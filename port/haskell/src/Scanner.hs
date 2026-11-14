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
  ( scannerInitialize,
    scannerStartLine,
    scannerForceIdentifier,
    scannerPeek,
    scannerPeek2,
    scannerAdvance,
    scannerPushSource,
    scannerPopSource,
    registerNameToNumber,
  )
where

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
scannerInitialize :: Spim -> String -> IO ()
scannerInitialize s input = S.set (scanner s) (initialScanState input)

-- | A new statement starts: the next token takes a new snapshot of the line.
scannerStartLine :: Spim -> IO ()
scannerStartLine s = S.modify (scanner s) (\st -> st {scanStateSnapshotSaved = False})

-- | The next word scanned is an identifier even if it is a keyword.
scannerForceIdentifier :: Spim -> IO ()
scannerForceIdentifier s = S.modify (scanner s) (\st -> st {scanStateForceIdentifier = True})

-- | Save the scanner and start on new input (C: @scanner_push_source@).
scannerPushSource :: Spim -> String -> IO ()
scannerPushSource s input = do
  st :: ScanState <- S.get (scanner s)
  S.modify (scannerStack s) (st :)
  scannerInitialize s input

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
  toks :: [ScanItem] <- gets scanStateTokens
  when (length toks < n) $ do
    item :: ScanItem <- scanOneToken s
    modify' (\st -> st {scanStateTokens = scanStateTokens st ++ [item]})
    ensureFilled s n

tokenOf :: ScanItem -> Token
tokenOf (ScanItem t _) = t

-- | The next token, without taking it.
scannerPeek :: Spim -> IO Token
scannerPeek s = withScanner s $ do
  ensureFilled s 1
  gets (tokenOf . head' . scanStateTokens)

-- | The token after the next.
scannerPeek2 :: Spim -> IO Token
scannerPeek2 s = withScanner s $ do
  ensureFilled s 2
  gets (tokenOf . head' . drop 1 . scanStateTokens)

head' :: [ScanItem] -> ScanItem
head' (x : _) = x
head' [] = ScanItem TokenEOF ValueNone

-- | Take the next token; a token with a value leaves it in 'scanValue'
-- (C: @scanner_advance@).
scannerAdvance :: Spim -> IO Token
scannerAdvance s = do
  (ScanItem t v :: ScanItem) <- withScanner s $ do
    ensureFilled s 1
    toks :: [ScanItem] <- gets scanStateTokens
    modify' (\st -> st {scanStateTokens = drop 1 toks})
    pure (head' toks)
  case t of
    TokenInteger -> S.set (scanValue s) v
    TokenIdentifier -> S.set (scanValue s) v
    TokenRegister -> S.set (scanValue s) v
    TokenFloatingPointRegister -> S.set (scanValue s) v
    TokenString -> S.set (scanValue s) v
    TokenFloatingPoint -> S.set (scanValue s) v
    _ -> pure ()
  pure t

-- Characters of the current line.

peekChar :: Scan Char
peekChar = gets (\st -> case scanStateRest st of (c : _) -> c; [] -> '\0')

peekChar2 :: Scan Char
peekChar2 = gets (\st -> case scanStateRest st of (_ : c : _) -> c; _ -> '\0')

nextChar :: Scan Char
nextChar = do
  st :: ScanState <- get
  case scanStateRest st of
    (c : rest) -> put st {scanStateRest = rest} >> pure c
    [] -> pure '\0'

atLineEnd :: Scan Bool
atLineEnd = gets (null . scanStateRest)

-- | Read the next line into the buffer (C: @fill_line_buf@, using
-- @fgets@ with a 4096-byte buffer: at most 4095 characters, up to and
-- including the newline).  A NUL byte ends the line early, as @strlen@
-- does in C.
fillLineBuffer :: Scan ()
fillLineBuffer = do
  st :: ScanState <- get
  if scanStateAtEof st
    then pure ()
    else
      if null (scanStateInput st)
        then put st {scanStateAtEof = True, scanStateLine = "", scanStateRest = ""}
        else do
          let chunk :: String
              rest :: String
              (chunk, rest) = takeLine 4095 (scanStateInput st)
              line :: String
              line = takeWhile (/= '\0') chunk
          put st {scanStateInput = rest, scanStateLine = line, scanStateRest = line}
  where
    takeLine :: Int -> String -> (String, String)
    takeLine 0 r = ([], r)
    takeLine _ [] = ([], [])
    takeLine n (c : r)
      | c == '\n' = ([c], r)
      | otherwise =
          let a :: String
              b :: String
              (a, b) = takeLine (n - 1) r
           in (c : a, b)

-- | Remember the current line for error messages, at its first token.
maybeSnapshotLine :: Scan ()
maybeSnapshotLine = do
  st :: ScanState <- get
  when (not (scanStateSnapshotSaved st)) $ do
    let line :: String
        line = scanStateLine st
        trimmed :: String
        trimmed = case reverse line of
          '\n' : rest -> reverse rest
          _ -> line
    put st {scanStateSnapshot = trimmed, scanStateSnapshotLineNo = scanStateLineNo st, scanStateSnapshotSaved = True}

-- | Scan one token (C: @scan_one_token@).
scanOneToken :: Spim -> Scan ScanItem
scanOneToken s = do
  forceIdentifier :: Bool <- gets scanStateForceIdentifier
  modify' (\st -> st {scanStateForceIdentifier = False})
  found :: Bool <- skipToToken
  if not found
    then pure (ScanItem TokenEOF ValueNone)
    else haveChar forceIdentifier
  where
    skipToToken :: Scan Bool
    skipToToken = do
      skipBlanks
      endOfLine :: Bool <- atLineEnd
      if not endOfLine
        then pure True
        else do
          eof :: Bool <- gets scanStateAtEof
          if eof
            then pure False
            else do
              fillLineBuffer
              st :: ScanState <- get
              if scanStateAtEof st && null (scanStateLine st)
                then pure False
                else put st {scanStateSnapshotSaved = False} >> skipToToken
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
    haveChar forceIdentifier = do
      c :: Char <- peekChar
      case c of
        '\n' -> do
          _ <- nextChar
          modify' (\st -> st {scanStateLineNo = scanStateLineNo st + 1})
          pure (ScanItem TokenNewline ValueNone)
        ';' -> nextChar >> pure (ScanItem TokenNewline ValueNone)
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
                    then scanIdentifier s forceIdentifier
                    else case c of
                      '$' -> scanRegister s
                      '"' -> scanString
                      '\'' -> scanChar
                      '?' -> nextChar >> pure (ScanItem TokenIdentifier (ValueString "?"))
                      _
                        | c `elem` "*/:()+->=" -> nextChar >> pure (ScanItem (TokenChar c) (ValueInteger (fromIntegral (ord c))))
                        | otherwise -> do
                            _ <- nextChar
                            lift (cError (printf "Unknown character in source: '%c' (0x%02x)\n" c (ord c)))
                            pure (ScanItem TokenNewline ValueNone)
    number :: Int -> Scan ScanItem
    number sign = do
      floatingPointLength :: Int <- gets (floatingPointLiteralLength . scanStateRest)
      if floatingPointLength > 0 then scanFloatingPoint sign floatingPointLength else scanInteger sign

-- | The length of a floating-point literal at the start of the text
-- (@[0-9]+[.][0-9]+([eE][+-]?[0-9]*)?@), or 0 (C: @scan_fp_check@).
floatingPointLiteralLength :: String -> Int
floatingPointLiteralLength text =
  let whole :: String
      r1 :: String
      (whole, r1) = span cDigit text
   in case r1 of
        ('.' : r2)
          | not (null whole) ->
              let frac :: String
                  r3 :: String
                  (frac, r3) = span cDigit r2
               in if null frac
                    then 0
                    else
                      let base :: Int
                          base = length whole + 1 + length frac
                       in case r3 of
                            (e : r4)
                              | e == 'e' || e == 'E' ->
                                  let sgn :: Int
                                      r5 :: String
                                      (sgn, r5) = case r4 of
                                        (p : r) | p == '+' || p == '-' -> (1, r)
                                        _ -> (0, r4)
                                   in base + 1 + sgn + length (takeWhile cDigit r5)
                            _ -> base
        _ -> 0

scanFloatingPoint :: Int -> Int -> Scan ScanItem
scanFloatingPoint sign byteLength = do
  st :: ScanState <- get
  let lit :: String
      rest :: String
      (lit, rest) = splitAt byteLength (scanStateRest st)
  put st {scanStateRest = rest}
  let value :: Double
      value = fromIntegral sign * cStrtod (take 63 lit)
  pure (ScanItem TokenFloatingPoint (ValueFloatingPoint value))

-- | A decimal or hexadecimal integer, wrapping at 32 bits (C: @scan_int@).
scanInteger :: Int -> Scan ScanItem
scanInteger sign = do
  c :: Char <- peekChar
  c2 :: Char <- peekChar2
  value :: Word32 <-
    if c == '0' && (c2 == 'x' || c2 == 'X')
      then nextChar >> nextChar >> digits cXdigit 16
      else digits cDigit 10
  pure (ScanItem TokenInteger (ValueInteger (fromIntegral (fromIntegral sign * value :: Word32))))
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
scanIdentifier s forceIdentifier = do
  st :: ScanState <- get
  let word :: String
      rest :: String
      (word, rest) = span (\c -> c == '_' || c == '.' || cAlnum c) (scanStateRest st)
      ident :: String
      ident = take 255 word
  put st {scanStateRest = rest}
  keyword :: Maybe Token <- if forceIdentifier then pure Nothing else lift (checkKeyword s ident)
  case keyword of
    Just t -> pure (ScanItem t ValueNone)
    Nothing -> constantOrIdentifier s ident

-- | Look a word up in the keyword table (C: @check_keyword@).
checkKeyword :: Spim -> String -> IO (Maybe Token)
checkKeyword s ident = do
  bare :: Bool <- S.get (optionBare s)
  pseudo :: Bool <- S.get (optionAcceptPseudo s)
  let allowPseudo :: Bool
      allowPseudo = not bare && pseudo
  pure $ case bsearchBy (\e -> compare ident (nvvName e)) keywordTable of
    Nothing -> Nothing
    Just e
      | not allowPseudo && nvvValue2 e == PSEUDO_OP -> Nothing
      | otherwise -> TokenOpcode <$> opcodeOfToken (nvvValue1 e)

-- | A defined constant scans as its number; anything else is an identifier.
constantOrIdentifier :: Spim -> String -> Scan ScanItem
constantOrIdentifier s ident = do
  ml :: Maybe Label <- lift (labelIsDefined s ident)
  isConstant :: Bool <- lift (maybe (pure False) (S.get . labelConstant) ml)
  case ml of
    Just l | isConstant -> do
      address :: Word32 <- lift (S.get (labelAddress l))
      pure (ScanItem TokenInteger (ValueInteger (fromIntegral address)))
    _ -> pure (ScanItem TokenIdentifier (ValueString ident))

-- | Register names (C: @register_tbl@), sorted for the binary search.
registerTable :: [(String, Int)]
registerTable =
  [ ("a0", 4),
    ("a1", 5),
    ("a2", 6),
    ("a3", 7),
    ("at", 1),
    ("fp", 30),
    ("gp", 28),
    ("k0", 26),
    ("k1", 27),
    ("kt0", 26),
    ("kt1", 27),
    ("ra", 31),
    ("s0", 16),
    ("s1", 17),
    ("s2", 18),
    ("s3", 19),
    ("s4", 20),
    ("s5", 21),
    ("s6", 22),
    ("s7", 23),
    ("s8", 30),
    ("sp", 29),
    ("t0", 8),
    ("t1", 9),
    ("t2", 10),
    ("t3", 11),
    ("t4", 12),
    ("t5", 13),
    ("t6", 14),
    ("t7", 15),
    ("t8", 24),
    ("t9", 25),
    ("v0", 2),
    ("v1", 3),
    ("zero", 0)
  ]

-- | A register name to its number: @3@, @f12@, @sp@; -1 if it is none
-- (C: @register_name_to_number@).
registerNameToNumber :: String -> Int
registerNameToNumber name = case name of
  [c1] | cDigit c1 -> atoi name
  [c1, c2] | cDigit c1 && cDigit c2 -> atoi name
  ('f' : c2 : _)
    | cDigit c2 ->
        -- \$f0..$f31 only; the digits are read unbounded, as C's strtol (then
        -- the range test) does, so a long number is not a register.
        let floatingPointRegister :: Integer
            floatingPointRegister = read (takeWhile cDigit (drop 1 name))
         in if floatingPointRegister <= 31 then fromIntegral floatingPointRegister else -1
  _ -> maybe (-1) id (lookup name registerTable)

-- | C's @atoi@ for the digits at the start of a string.
atoi :: String -> Int
atoi string = fromIntegral (foldl (\acc d -> acc * 10 + fromIntegral (digitToInt d)) 0 (takeWhile cDigit string) :: Int32)

-- | A @$@ register (C: @scan_register@).
scanRegister :: Spim -> Scan ScanItem
scanRegister s = do
  _ <- nextChar
  st :: ScanState <- get
  let word :: String
      rest :: String
      (word, rest) = span (\c -> c == '_' || c == '.' || c == '$' || cAlnum c) (scanStateRest st)
      name :: String
      name = take 63 word
  put st {scanStateRest = rest}
  let n :: Int
      n = registerNameToNumber name
  case name of
    ('f' : c2 : _) | n /= -1 && c2 /= 'p' -> pure (ScanItem TokenFloatingPointRegister (ValueInteger (fromIntegral n)))
    ['f'] | n /= -1 -> pure (ScanItem TokenFloatingPointRegister (ValueInteger (fromIntegral n)))
    _
      | n >= 0 && n < 32 -> pure (ScanItem TokenRegister (ValueInteger (fromIntegral n)))
      | otherwise -> constantOrIdentifier s name

-- | A string literal, with its escapes decoded (C: @scan_string@).
scanString :: Scan ScanItem
scanString = do
  _ <- nextChar
  body :: String <- collect
  c :: Char <- peekChar
  when (c == '"') (() <$ nextChar)
  decoded :: String <- lift (decodeString body)
  pure (ScanItem TokenString (ValueString decoded))
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
decodeString string = case string of
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
    numeric base ok message start shiftFirst rest = case rest of
      (c2 : c3 : more) -> do
        b1 :: Int <- digitValue base ok message c2
        b2 :: Int <- digitValue base ok message c3
        let b :: Int
            b = if base == 8 then start + (b1 `shiftL` 3) + b2 else ((start + b1) `shiftL` 4) .|. b2
        _ <- pure shiftFirst
        (chr (b `mod` 256) :) <$> decodeString more
      _ -> ('\\' :) <$> decodeString (drop 1 string)
    digitValue :: Int -> (Char -> Bool) -> String -> Char -> IO Int
    digitValue _ ok message c
      | ok c = pure (digitToInt c)
      | otherwise = cError message >> pure 0

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
  pure (ScanItem TokenInteger (ValueInteger (fromIntegral v)))
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
              let message :: String
                  message = "Bad character in \\X construct in char literal\n"
              h :: Int <- if cXdigit hi then pure (digitToInt hi `shiftL` 4) else lift (cError message) >> pure 0
              l :: Int <- if cXdigit lo then pure (digitToInt lo) else lift (cError message) >> pure 0
              _ <- nextChar
              _ <- nextChar
              pure (h .|. l)
          | otherwise -> do
              lift (cError ("Bad escape character \\" ++ [first] ++ " in char literal\n"))
              pure 0
