-- | The lexical scanner (C: src/scanner.c).
--
-- The scanner reads one line at a time and cuts it into tokens.  The parser
-- looks up to two tokens ahead ('scannerPeek', 'scannerPeek2') before it
-- takes one ('scannerAdvance').  A word is classified when it is scanned,
-- and the answer depends on the moment: a keyword unless the parser has
-- just asked for an identifier ('scannerForceIdentifier'), a pseudo-op only
-- when pseudo-ops are allowed, a number if it names a constant already
-- defined.  So the look-ahead buffer here works exactly as the C one, to
-- classify every word at the same moment.
--
-- The scanning functions run in @StateT ScanState IO@: the state is the
-- scanner's record, and IO is for printing errors and asking the symbol
-- table about constants.
module Scanner
  ( scannerInit
  , scannerStartLine
  , scannerForceIdentifier
  , scannerPeek
  , scannerPeek2
  , scannerAdvance
  , scannerPushSource
  , scannerPopSource
  , registerNameToNumber
  ) where

import CFormat (cStrtod)
import Control.Monad (when)
import Control.Monad.State.Strict (StateT, get, gets, lift, modify', put, runStateT)
import Data.Bits (shiftL, (.|.))
import Data.Char (chr, digitToInt, isAlpha, isAlphaNum, isAscii, isDigit, isHexDigit, ord)
import Data.Int (Int32)
import Data.Word (Word32)
import OpcodeTables
import OpcodeTypes (OpType (..))
import Output (cError)
import State hiding (get, modify)
import qualified State as S
import SymbolTable (labelIsDefined)
import Text.Printf (printf)
import Types (Label (..))

type Scan a = StateT ScanState IO a

-- | C's character classes, which in the C locale know only ASCII.
cDigit, cAlpha, cAlnum, cXdigit :: Char -> Bool
cDigit c = isAscii c && isDigit c
cAlpha c = isAscii c && isAlpha c
cAlnum c = isAscii c && isAlphaNum c
cXdigit c = isAscii c && isHexDigit c

-- | Start scanning new input (C: @scanner_init@).
scannerInit :: Spim -> String -> IO ()
scannerInit s input = S.set (scanner s) (initialScanState input)

-- | A new statement starts: the next token takes a new snapshot of the line.
scannerStartLine :: Spim -> IO ()
scannerStartLine s = S.modify (scanner s) (\st -> st {ssSnapshotSaved = False})

-- | The next word scanned is an identifier even if it is a keyword.
scannerForceIdentifier :: Spim -> IO ()
scannerForceIdentifier s = S.modify (scanner s) (\st -> st {ssForceId = True})

-- | Save the scanner and start on new input (C: @scanner_push_source@).
scannerPushSource :: Spim -> String -> IO ()
scannerPushSource s input = do
  st :: ScanState <- S.get (scanner s)
  S.modify (scannerStack s) (st :)
  scannerInit s input

-- | Go back to the saved scanner, if there is one.
scannerPopSource :: Spim -> IO ()
scannerPopSource s = do
  stack :: [ScanState] <- S.get (scannerStack s)
  case stack of
    (st : rest) -> S.set (scanner s) st >> S.set (scannerStack s) rest
    [] -> pure ()

-- | Run a scanning action on the machine's scanner.
withScanner :: Spim -> Scan a -> IO a
withScanner s act = do
  st :: ScanState <- S.get (scanner s)
  (r, st') :: (a, ScanState) <- runStateT act st
  S.set (scanner s) st'
  pure r

-- | Make sure the look-ahead buffer holds at least n tokens.
ensureFilled :: Spim -> Int -> Scan ()
ensureFilled s n = do
  toks :: [ScanItem] <- gets ssTokens
  when (length toks < n) $ do
    item :: ScanItem <- scanOneToken s
    modify' (\st -> st {ssTokens = ssTokens st ++ [item]})
    ensureFilled s n

tokenOf :: ScanItem -> Token
tokenOf (ScanItem t _) = t

-- | The next token, without taking it.
scannerPeek :: Spim -> IO Token
scannerPeek s = withScanner s $ do
  ensureFilled s 1
  gets (tokenOf . head' . ssTokens)

-- | The token after the next.
scannerPeek2 :: Spim -> IO Token
scannerPeek2 s = withScanner s $ do
  ensureFilled s 2
  gets (tokenOf . head' . drop 1 . ssTokens)

head' :: [ScanItem] -> ScanItem
head' (x : _) = x
head' [] = ScanItem TokEOF VNone

-- | Take the next token; a token with a value leaves it in 'scanValue'
-- (C: @scanner_advance@).
scannerAdvance :: Spim -> IO Token
scannerAdvance s = do
  (ScanItem t v :: ScanItem) <- withScanner s $ do
    ensureFilled s 1
    toks :: [ScanItem] <- gets ssTokens
    modify' (\st -> st {ssTokens = drop 1 toks})
    pure (head' toks)
  case t of
    TokInt -> S.set (scanValue s) v
    TokId -> S.set (scanValue s) v
    TokReg -> S.set (scanValue s) v
    TokFpReg -> S.set (scanValue s) v
    TokStr -> S.set (scanValue s) v
    TokFp -> S.set (scanValue s) v
    _ -> pure ()
  pure t

-- Characters of the current line.

peekChar :: Scan Char
peekChar = gets (\st -> case ssRest st of (c : _) -> c; [] -> '\0')

peekChar2 :: Scan Char
peekChar2 = gets (\st -> case ssRest st of (_ : c : _) -> c; _ -> '\0')

nextChar :: Scan Char
nextChar = do
  st :: ScanState <- get
  case ssRest st of
    (c : rest) -> put st {ssRest = rest} >> pure c
    [] -> pure '\0'

atLineEnd :: Scan Bool
atLineEnd = gets (null . ssRest)

-- | Read the next line into the buffer (C: @fill_line_buf@, using
-- @fgets@ with a 4096-byte buffer: at most 4095 characters, up to and
-- including the newline).  A NUL byte ends the line early, as @strlen@
-- does in C.
fillLineBuf :: Scan ()
fillLineBuf = do
  st :: ScanState <- get
  if ssAtEof st
    then pure ()
    else
      if null (ssInput st)
        then put st {ssAtEof = True, ssLine = "", ssRest = ""}
        else do
          let chunk :: String
              rest :: String
              (chunk, rest) = takeLine 4095 (ssInput st)
              line :: String
              line = takeWhile (/= '\0') chunk
          put st {ssInput = rest, ssLine = line, ssRest = line}
  where
    takeLine :: Int -> String -> (String, String)
    takeLine 0 r = ([], r)
    takeLine _ [] = ([], [])
    takeLine n (c : r)
      | c == '\n' = ([c], r)
      | otherwise = let a :: String
                        b :: String
                        (a, b) = takeLine (n - 1) r in (c : a, b)

-- | Remember the current line for error messages, at its first token.
maybeSnapshotLine :: Scan ()
maybeSnapshotLine = do
  st :: ScanState <- get
  when (not (ssSnapshotSaved st)) $ do
    let line :: String
        line = ssLine st
        trimmed :: String
        trimmed = case reverse line of
          '\n' : rest -> reverse rest
          _ -> line
    put st {ssSnapshot = trimmed, ssSnapshotLineNo = ssLineNo st, ssSnapshotSaved = True}

-- | Scan one token (C: @scan_one_token@).
scanOneToken :: Spim -> Scan ScanItem
scanOneToken s = do
  forceId :: Bool <- gets ssForceId
  modify' (\st -> st {ssForceId = False})
  found :: Bool <- skipToToken
  if not found
    then pure (ScanItem TokEOF VNone)
    else haveChar forceId
  where
    skipToToken :: Scan Bool
    skipToToken = do
      skipBlanks
      endOfLine :: Bool <- atLineEnd
      if not endOfLine
        then pure True
        else do
          eof :: Bool <- gets ssAtEof
          if eof
            then pure False
            else do
              fillLineBuf
              st :: ScanState <- get
              if ssAtEof st && null (ssLine st)
                then pure False
                else put st {ssSnapshotSaved = False} >> skipToToken
    skipBlanks :: Scan ()
    skipBlanks = do
      c :: Char <- peekChar
      endOfLine :: Bool <- atLineEnd
      if endOfLine
        then pure ()
        else case c of
          _ | c `elem` " \t\r," -> nextChar >> skipBlanks
          '#' -> skipComment >> skipBlanks
          _ -> pure ()
    skipComment :: Scan ()
    skipComment = do
      c :: Char <- peekChar
      if c /= '\0' && c /= '\n' then nextChar >> skipComment else pure ()
    haveChar :: Bool -> Scan ScanItem
    haveChar forceId = do
      c :: Char <- peekChar
      case c of
        '\n' -> do
          _ <- nextChar
          modify' (\st -> st {ssLineNo = ssLineNo st + 1})
          pure (ScanItem TokNL VNone)
        ';' -> nextChar >> pure (ScanItem TokNL VNone)
        _ -> do
          maybeSnapshotLine
          c2 :: Char <- peekChar2
          if c == '-' && cDigit c2
            then nextChar >> number (-1)
            else
              if cDigit c
                then number 1
                else
                  if c == '_' || c == '.' || cAlpha c
                    then scanIdentifier s forceId
                    else case c of
                      '$' -> scanRegister s
                      '"' -> scanString
                      '\'' -> scanChar
                      '?' -> nextChar >> pure (ScanItem TokId (VStr "?"))
                      _
                        | c `elem` "*/:()+->=" -> nextChar >> pure (ScanItem (TokChar c) (VInt (fromIntegral (ord c))))
                        | otherwise -> do
                            _ <- nextChar
                            lift (cError (printf "Unknown character in source: '%c' (0x%02x)\n" c (ord c)))
                            pure (ScanItem TokNL VNone)
    number :: Int -> Scan ScanItem
    number sign = do
      fpLen :: Int <- gets (fpLiteralLength . ssRest)
      if fpLen > 0 then scanFp sign fpLen else scanInt sign

-- | The length of a floating-point literal at the start of the text
-- (@[0-9]+[.][0-9]+([eE][+-]?[0-9]*)?@), or 0 (C: @scan_fp_check@).
fpLiteralLength :: String -> Int
fpLiteralLength text =
  let whole :: String
      r1 :: String
      (whole, r1) = span cDigit text
   in case r1 of
        ('.' : r2) | not (null whole) ->
          let frac :: String
              r3 :: String
              (frac, r3) = span cDigit r2
           in if null frac
                then 0
                else
                  let base :: Int
                      base = length whole + 1 + length frac
                   in case r3 of
                        (e : r4) | e == 'e' || e == 'E' ->
                          let sgn :: Int
                              r5 :: String
                              (sgn, r5) = case r4 of
                                (p : r) | p == '+' || p == '-' -> (1, r)
                                _ -> (0, r4)
                           in base + 1 + sgn + length (takeWhile cDigit r5)
                        _ -> base
        _ -> 0

scanFp :: Int -> Int -> Scan ScanItem
scanFp sign len = do
  st :: ScanState <- get
  let lit :: String
      rest :: String
      (lit, rest) = splitAt len (ssRest st)
  put st {ssRest = rest}
  let value :: Double
      value = fromIntegral sign * cStrtod (take 63 lit)
  pure (ScanItem TokFp (VFp value))

-- | A decimal or hexadecimal integer, wrapping at 32 bits (C: @scan_int@).
scanInt :: Int -> Scan ScanItem
scanInt sign = do
  c :: Char <- peekChar
  c2 :: Char <- peekChar2
  value :: Word32 <-
    if c == '0' && (c2 == 'x' || c2 == 'X')
      then nextChar >> nextChar >> digits cXdigit 16
      else digits cDigit 10
  pure (ScanItem TokInt (VInt (fromIntegral (fromIntegral sign * value :: Word32))))
  where
    digits :: (Char -> Bool) -> Word32 -> Scan Word32
    digits ok base = go 0
      where
        go :: Word32 -> StateT ScanState IO Word32
        go acc = do
          d :: Char <- peekChar
          if ok d then nextChar >> go (acc * base + fromIntegral (digitToInt d)) else pure acc

-- | A word: a keyword, a constant's value, or an identifier
-- (C: @scan_identifier@).
scanIdentifier :: Spim -> Bool -> Scan ScanItem
scanIdentifier s forceId = do
  st :: ScanState <- get
  let word :: String
      rest :: String
      (word, rest) = span (\c -> c == '_' || c == '.' || cAlnum c) (ssRest st)
      ident :: String
      ident = take 255 word
  put st {ssRest = rest}
  keyword :: Maybe Token <- if forceId then pure Nothing else lift (checkKeyword s ident)
  case keyword of
    Just t -> pure (ScanItem t VNone)
    Nothing -> constOrId s ident

-- | Look a word up in the keyword table (C: @check_keyword@).
checkKeyword :: Spim -> String -> IO (Maybe Token)
checkKeyword s ident = do
  bare :: Bool <- S.get (optBare s)
  pseudo :: Bool <- S.get (optAcceptPseudo s)
  let allowPseudo :: Bool
      allowPseudo = not bare && pseudo
  pure $ case bsearchBy (\e -> compare ident (nvvName e)) keywordTable of
    Nothing -> Nothing
    Just e
      | not allowPseudo && nvvValue2 e == PSEUDO_OP -> Nothing
      | otherwise -> TokOp <$> opcodeOfToken (nvvValue1 e)

-- | A defined constant scans as its number; anything else is an identifier.
constOrId :: Spim -> String -> Scan ScanItem
constOrId s ident = do
  ml :: Maybe Label <- lift (labelIsDefined s ident)
  isConst :: Bool <- lift (maybe (pure False) (S.get . lblConst) ml)
  case ml of
    Just l | isConst -> do
      addr :: Word32 <- lift (S.get (lblAddr l))
      pure (ScanItem TokInt (VInt (fromIntegral addr)))
    _ -> pure (ScanItem TokId (VStr ident))

-- | Register names (C: @register_tbl@), sorted for the binary search.
registerTable :: [(String, Int)]
registerTable =
  [ ("a0", 4), ("a1", 5), ("a2", 6), ("a3", 7), ("at", 1), ("fp", 30), ("gp", 28), ("k0", 26)
  , ("k1", 27), ("kt0", 26), ("kt1", 27), ("ra", 31), ("s0", 16), ("s1", 17), ("s2", 18), ("s3", 19)
  , ("s4", 20), ("s5", 21), ("s6", 22), ("s7", 23), ("s8", 30), ("sp", 29), ("t0", 8), ("t1", 9)
  , ("t2", 10), ("t3", 11), ("t4", 12), ("t5", 13), ("t6", 14), ("t7", 15), ("t8", 24), ("t9", 25)
  , ("v0", 2), ("v1", 3), ("zero", 0)
  ]

-- | A register name to its number: @3@, @f12@, @sp@; -1 if it is none
-- (C: @register_name_to_number@).
registerNameToNumber :: String -> Int
registerNameToNumber name = case name of
  [c1] | cDigit c1 -> atoi name
  [c1, c2] | cDigit c1 && cDigit c2 -> atoi name
  ('f' : c2 : _)
    | cDigit c2 ->
        -- $f0..$f31 only; the digits are read unbounded, as C's strtol (then
        -- the range test) does, so a long number is not a register.
        let fpReg :: Integer
            fpReg = read (takeWhile cDigit (drop 1 name))
         in if fpReg <= 31 then fromIntegral fpReg else -1
  _ -> maybe (-1) id (lookup name registerTable)

-- | C's @atoi@ for the digits at the start of a string.
atoi :: String -> Int
atoi str = fromIntegral (foldl (\acc d -> acc * 10 + fromIntegral (digitToInt d)) 0 (takeWhile cDigit str) :: Int32)

-- | A @$@ register (C: @scan_register@).
scanRegister :: Spim -> Scan ScanItem
scanRegister s = do
  _ <- nextChar
  st :: ScanState <- get
  let word :: String
      rest :: String
      (word, rest) = span (\c -> c == '_' || c == '.' || c == '$' || cAlnum c) (ssRest st)
      name :: String
      name = take 63 word
  put st {ssRest = rest}
  let n :: Int
      n = registerNameToNumber name
  case name of
    ('f' : c2 : _) | n /= -1 && c2 /= 'p' -> pure (ScanItem TokFpReg (VInt (fromIntegral n)))
    ['f'] | n /= -1 -> pure (ScanItem TokFpReg (VInt (fromIntegral n)))
    _
      | n >= 0 && n < 32 -> pure (ScanItem TokReg (VInt (fromIntegral n)))
      | otherwise -> constOrId s name

-- | A string literal, with its escapes decoded (C: @scan_string@).
scanString :: Scan ScanItem
scanString = do
  _ <- nextChar
  body :: String <- collect
  c :: Char <- peekChar
  when (c == '"') (() <$ nextChar)
  decoded :: String <- lift (decodeString body)
  pure (ScanItem TokStr (VStr decoded))
  where
    collect :: StateT ScanState IO String
    collect = do
      c :: Char <- peekChar
      c2 :: Char <- peekChar2
      if c == '\0' || c == '"'
        then pure []
        else
          if c == '\\' && c2 /= '\0'
            then do
              a :: Char <- nextChar
              b :: Char <- nextChar
              (\r -> a : b : r) <$> collect
            else do
              a :: Char <- nextChar
              (a :) <$> collect

-- | Decode @\\n@, @\\t@, @\\\"@, three-digit octal @\\NNN@ and hexadecimal
-- @\\XHH@ in a string literal (C: @decode_string@).
decodeString :: String -> IO String
decodeString str = case str of
  [] -> pure []
  ['\\'] -> pure "\\"
  ('\\' : n : rest) -> case n of
    'n' -> ('\n' :) <$> decodeString rest
    't' -> ('\t' :) <$> decodeString rest
    '"' -> ('"' :) <$> decodeString rest
    _
      | n >= '0' && n <= '3' -> numeric 8 (\c -> c >= '0' && c <= '7') "Bad character in \\ooo construct in string\n" ((ord n - ord '0') `shiftL` 6) 3 rest
      | n == 'X' -> numeric 16 cXdigit "Bad character in \\X construct in string\n" 0 4 rest
      | otherwise -> ('\\' :) <$> decodeString (n : rest)
  (c : rest) -> (c :) <$> decodeString rest
  where
    -- Two more digits after the first; too few characters left and the
    -- backslash is copied as it is.
    numeric :: Int -> (Char -> Bool) -> String -> Int -> Int -> String -> IO String
    numeric base ok msg start shiftFirst rest = case rest of
      (c2 : c3 : more) -> do
        b1 :: Int <- digitValue base ok msg c2
        b2 :: Int <- digitValue base ok msg c3
        let b :: Int
            b = if base == 8 then start + (b1 `shiftL` 3) + b2 else ((start + b1) `shiftL` 4) .|. b2
        _ <- pure shiftFirst
        (chr (b `mod` 256) :) <$> decodeString more
      _ -> ('\\' :) <$> decodeString (drop 1 str)
    digitValue :: Int -> (Char -> Bool) -> String -> Char -> IO Int
    digitValue _ ok msg c
      | ok c = pure (digitToInt c)
      | otherwise = cError msg >> pure 0

-- | A character literal like @'a'@ or @'\\n'@ (C: @scan_char@).
scanChar :: Scan ScanItem
scanChar = do
  _ <- nextChar
  c :: Char <- peekChar
  v :: Int <-
    if c == '\\'
      then nextChar >> escapeChar
      else ord <$> nextChar
  q :: Char <- peekChar
  when (q == '\'') (() <$ nextChar)
  pure (ScanItem TokInt (VInt (fromIntegral v)))
  where
    escapeChar :: StateT ScanState IO Int
    escapeChar = do
      first :: Char <- nextChar
      case first of
        'a' -> pure 7
        'b' -> pure 8
        'f' -> pure 12
        'n' -> pure 10
        'r' -> pure 13
        't' -> pure 9
        '\\' -> pure 92
        '"' -> pure 34
        '\'' -> pure 39
        _
          | first == 'x' || first == 'X' -> do
              hi :: Char <- peekChar
              lo :: Char <- peekChar2
              let msg :: String
                  msg = "Bad character in \\X construct in char literal\n"
              h :: Int <- if cXdigit hi then pure (digitToInt hi `shiftL` 4) else lift (cError msg) >> pure 0
              l :: Int <- if cXdigit lo then pure (digitToInt lo) else lift (cError msg) >> pure 0
              _ <- nextChar
              _ <- nextChar
              pure (h .|. l)
          | otherwise -> do
              lift (cError ("Bad escape character \\" ++ [first] ++ " in char literal\n"))
              pure 0
