-- | Print floating-point numbers exactly as glibc's @printf@ does.
--
-- The C engine prints floats in five formats: @%.8f@ and @%.18g@ (the
-- print_float / print_double syscalls), @%.17g@ (the JSON AST dump), @%g@
-- (the text AST dump and the REPL's @print $f0@), and @%#-13.6g@ (the FP
-- register display).
--
-- Haskell's own @Text.Printf@ and @Numeric.showFFloat@ start from the
-- /shortest/ digits that read back as the same Double, then pad with
-- zeros: @0.1 + 0.2@ prints as @0.300000000000000040@.  glibc instead
-- rounds the Double's /exact/ binary value: @0.300000000000000044@.  So this
-- module works from the exact value: 'toRational' turns a Double into the
-- fraction it really is, and from there every format is integer arithmetic.
-- Tested byte-identical to glibc on 158,029 values
-- (port/haskell/tests/check-format.sh, the meson test haskell-float-format).
module CFormat
  ( formatF
  , formatG
  , formatGAlt
  , leftIn
  , cStrtod
  ) where

import Data.Char (digitToInt, isAlphaNum, isDigit, isHexDigit, toLower)
import Data.Ratio (denominator, numerator, (%))
import GHC.Float (castDoubleToWord64, castWord64ToDouble)
import Data.Bits (testBit, (.&.), (.|.))
import Data.Word (Word64)

-- | Round a non-negative rational to the nearest integer, ties to even
-- (glibc's default rounding mode, applied to the exact value).
roundHalfEven :: Rational -> Integer
roundHalfEven r =
  let q :: Integer
      f :: Rational
      (q, f) = properFraction r :: (Integer, Rational)
   in case compare f (1 / 2) of
        LT -> q
        GT -> q + 1
        EQ -> if even q then q else q + 1

-- | Largest x with 10^x <= r, for r > 0, found by exact comparison.
decimalExponent :: Rational -> Int
decimalExponent r = go (length (show (numerator r)) - length (show (denominator r)))
  where
    go :: Int -> Int
    go x
      | 10 ^^ x > r = go (x - 1)
      | 10 ^^ (x + 1) <= r = go (x + 1)
      | otherwise = x

-- | inf, -inf, nan and -nan (the NaN's sign is its sign bit), or Nothing.
special :: Double -> Maybe String
special x
  | isNaN x = Just (if testBit (castDoubleToWord64 x) 63 then "-nan" else "nan")
  | isInfinite x = Just (if x < 0 then "-inf" else "inf")
  | otherwise = Nothing

-- | Drop trailing fraction zeros, and the point if nothing follows it.
stripZeros :: String -> String
stripZeros s
  | '.' `notElem` s = s
  | otherwise = case dropWhile (== '0') (reverse s) of
      '.' : rest -> reverse rest
      rest -> reverse rest

-- | "-" for a negative number or negative zero.
sign :: Double -> String
sign x = if x < 0 || isNegativeZero x then "-" else ""

-- | The digits of n, left-padded with zeros to at least w characters.
padded :: Int -> Integer -> String
padded w n = let s :: String
                 s = show n in replicate (w - length s) '0' ++ s

-- | C's @%.<p>f@.
formatF :: Int -> Double -> String
formatF p x = case special x of
  Just s -> s
  Nothing ->
    let n :: Integer
        n = roundHalfEven (abs (toRational x) * 10 ^ p)
        s :: String
        s = padded (p + 1) n
        whole :: String
        frac :: String
        (whole, frac) = splitAt (length s - p) s
     in sign x ++ whole ++ (if p == 0 then "" else '.' : frac)

-- | C's @%.<p>g@.
formatG :: Int -> Double -> String
formatG = formatGAlt False

-- | C's @%.<p>g@, or @%#.<p>g@ when the first argument is True (the @#@
-- flag keeps trailing zeros and the decimal point).
formatGAlt :: Bool -> Int -> Double -> String
formatGAlt alt p0 x = case special x of
  Just s -> s
  Nothing ->
    let p :: Int
        p = max 1 p0
        r :: Rational
        r = abs (toRational x)
        e :: Int
        n :: Integer
        (e, n)
          | r == 0 = (0, 0)
          | otherwise =
              let e0 :: Int
                  e0 = decimalExponent r
                  n0 :: Integer
                  n0 = roundHalfEven (r / 10 ^^ (e0 - p + 1))
               in if n0 >= 10 ^ p then (e0 + 1, n0 `quot` 10) else (e0, n0)
        withPoint :: String -> Int -> String
        withPoint s k =
          let a :: String
              b :: String
              (a, b) = splitAt k s
           in if null b && not alt then a else a ++ "." ++ b
        strip :: String -> String
        strip = if alt then id else stripZeros
     in sign x
          ++ if e < -4 || e >= p
            then
              let ds :: String
                  ds = padded p n
                  mant :: String
                  mant = strip (withPoint ds 1)
                  ex :: Int
                  ex = abs e
               in mant ++ "e" ++ (if e < 0 then "-" else "+")
                    ++ (if ex < 10 then "0" else "") ++ show ex
            else
              let decimals :: Int
                  decimals = p - 1 - e
                  ds :: String
                  ds = padded (decimals + 1) n
               in strip (withPoint ds (length ds - decimals))

-- | C's @%-<w>s@: left-justify in a field of w characters.
leftIn :: Int -> String -> String
leftIn w s = s ++ replicate (w - length s) ' '

-- | C's @atof@ (@strtod@ without the end pointer): the longest prefix of
-- the text that reads as a number, after leading white space; 0 if none.
-- Decimal and hexadecimal forms, @inf@/@infinity@ and @nan@, any case.
-- The conversion is exact, then rounded once (as glibc does).
cStrtod :: String -> Double
cStrtod text =
  let s :: String
      s = dropWhile (`elem` " \t\n\v\f\r") text
      neg :: Bool
      body :: String
      (neg, body) = case s of
        ('-' : r) -> (True, r)
        ('+' : r) -> (False, r)
        _ -> (False, s)
      apply :: Double -> Double
      apply v = if neg then negate v else v
      lower :: String
      lower = map toLower body
   in case () of
        _
          | take 3 lower == "inf" -> apply (1 / 0)
          | take 3 lower == "nan" -> apply (nanWithPayload (drop 3 body))
          | take 2 lower == "0x" && hexStarts (drop 2 body) -> apply (hexValue (drop 2 body))
          | otherwise -> maybe 0 apply (decimalValue body)
  where
    hexStarts :: String -> Bool
    hexStarts r = case r of
      (c : _) | isHexDigit c -> True
      ('.' : c : _) | isHexDigit c -> True
      _ -> False

-- | glibc's NaN: the quiet NaN, positive, carrying the number written in
-- parentheses after it (@nan(123)@), if any, in its low mantissa bits.
nanWithPayload :: String -> Double
nanWithPayload rest = castWord64ToDouble (0x7ff8000000000000 .|. (payload .&. 0x7ffffffffffff))
  where
    payload :: Word64
    payload = case rest of
      ('(' : r) | (inside, ')' : _) <- span (\c -> isAlphaNum c || c == '_') r -> number inside
      _ -> 0
    number :: String -> Word64
    number t = case map toLower t of
      ('0' : 'x' : h) | not (null h), all isHexDigit h -> foldl (\a c -> a * 16 + fromIntegral (digitToInt c)) 0 h
      ('0' : o) | all (`elem` "01234567") o -> foldl (\a c -> a * 8 + fromIntegral (digitToInt c)) 0 o
      d | not (null d), all isDigit d -> read d
      _ -> 0

-- | The value of a decimal number at the start of the text, if there is one.
decimalValue :: String -> Maybe Double
decimalValue s =
  let intDigits :: String
      r1 :: String
      (intDigits, r1) = span isDigit s
      fracDigits :: String
      r2 :: String
      (fracDigits, r2) = case r1 of
        ('.' : r) -> span isDigit r
        _ -> ("", r1)
      expo :: Int
      expo = case r2 of
        (e : r) | e `elem` "eE" -> exponentPart r
        _ -> 0
      digits :: String
      digits = intDigits ++ fracDigits
   in if null digits
        then Nothing
        else Just (fromDecimal (read digits) (expo - length fracDigits))
  where
    exponentPart :: String -> Int
    exponentPart r =
      let eneg :: Bool
          r' :: String
          (eneg, r') = case r of
            ('-' : t) -> (True, t)
            ('+' : t) -> (False, t)
            _ -> (False, r)
          ds :: String
          ds = takeWhile isDigit r'
       in if null ds then 0 else (if eneg then negate else id) (read (take 9 ds) :: Int)

-- | m * 10^e, rounded once to the nearest Double (ties to even).
fromDecimal :: Integer -> Int -> Double
fromDecimal 0 _ = 0
fromDecimal m e
  | e + length (show m) > 330 = 1 / 0
  | e + length (show m) < -360 = 0
  | e >= 0 = fromRational (fromInteger (m * 10 ^ e))
  | otherwise = fromRational (m % (10 ^ negate e))

-- | The value of a hexadecimal number (after the "0x") at the start of
-- the text: hex digits, an optional fraction, an optional binary exponent.
hexValue :: String -> Double
hexValue s =
  let intDigits :: String
      r1 :: String
      (intDigits, r1) = span isHexDigit s
      fracDigits :: String
      r2 :: String
      (fracDigits, r2) = case r1 of
        ('.' : r) -> span isHexDigit r
        _ -> ("", r1)
      expo :: Int
      expo = case r2 of
        (p : r) | p `elem` "pP" -> binaryExponent r
        _ -> 0
      m :: Integer
      m = foldl (\acc c -> acc * 16 + toInteger (digitToInt c)) 0 (intDigits ++ fracDigits)
      e :: Int
      e = expo - 4 * length fracDigits
   in if m == 0 then 0
      else if e > 1100 then 1 / 0
      else if e < -1200 then 0
      else if e >= 0 then fromRational (fromInteger (m * 2 ^ e))
      else fromRational (m % (2 ^ negate e))
  where
    binaryExponent :: String -> Int
    binaryExponent r =
      let eneg :: Bool
          r' :: String
          (eneg, r') = case r of
            ('-' : t) -> (True, t)
            ('+' : t) -> (False, t)
            _ -> (False, r)
          ds :: String
          ds = takeWhile isDigit r'
       in if null ds then 0 else (if eneg then negate else id) (read (take 9 ds) :: Int)
