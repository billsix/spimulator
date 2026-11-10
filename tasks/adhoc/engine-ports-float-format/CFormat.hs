-- glibc-compatible %.<p>g and %.<p>f for Double, from the exact binary value.
module Main (main) where

import Data.Ratio (denominator, numerator)
import Data.Word (Word64)
import GHC.Float (castWord64ToDouble)
import Numeric (readHex)

-- Round a non-negative rational to the nearest integer, ties to even
-- (glibc's default rounding mode, applied to the exact value).
roundHalfEven :: Rational -> Integer
roundHalfEven r =
  let (q, f) = properFraction r :: (Integer, Rational)
   in case compare f (1 / 2) of
        LT -> q
        GT -> q + 1
        EQ -> if even q then q else q + 1

-- Largest x with 10^x <= r, for r > 0, by exact comparison.
decimalExponent :: Rational -> Int
decimalExponent r = go (length (show (numerator r)) - length (show (denominator r)))
  where
    go x
      | 10 ^^ x > r = go (x - 1)
      | 10 ^^ (x + 1) <= r = go (x + 1)
      | otherwise = x

-- inf only; NaN's sign lives in its bits, so the caller formats NaN.
special :: Double -> Maybe String
special x
  | isInfinite x = Just (if x < 0 then "-inf" else "inf")
  | otherwise = Nothing

-- drop trailing fraction zeros, and the point if nothing follows it
stripZeros :: String -> String
stripZeros s
  | '.' `notElem` s = s
  | otherwise = case reverse (dropWhile (== '0') (reverse s)) of
      str | last str == '.' -> init str
      str -> str

sign :: Double -> String
sign x = if x < 0 || isNegativeZero x then "-" else ""

-- digits of n, left-padded with zeros to at least w characters
padded :: Int -> Integer -> String
padded w n = let s = show n in replicate (w - length s) '0' ++ s

-- "%.<p>f"
formatF :: Int -> Double -> String
formatF p x = case special x of
  Just s -> s
  Nothing ->
    let n = roundHalfEven (abs (toRational x) * 10 ^ p)
        s = padded (p + 1) n
        (whole, frac) = splitAt (length s - p) s
     in sign x ++ whole ++ (if p == 0 then "" else '.' : frac)

-- "%.<p>g", or "%#.<p>g" when alt is True (keep trailing zeros and the point)
formatG :: Int -> Double -> String
formatG = formatGAlt False

formatGAlt :: Bool -> Int -> Double -> String
formatGAlt alt p0 x = case special x of
  Just s -> s
  Nothing ->
    let p = max 1 p0
        r = abs (toRational x)
        (e, n)
          | r == 0 = (0, 0)
          | otherwise =
              let e0 = decimalExponent r
                  n0 = roundHalfEven (r / 10 ^^ (e0 - p + 1))
               in if n0 >= 10 ^ p then (e0 + 1, n0 `div` 10) else (e0, n0)
        withPoint s k = let (a, b) = splitAt k s in if null b && not alt then a else a ++ "." ++ b
        strip = if alt then id else stripZeros
     in sign x
          ++ if e < -4 || e >= p
            then
              let ds = padded p n
                  mant = strip (withPoint ds 1)
                  ex = abs e
               in mant ++ "e" ++ (if e < 0 then "-" else "+") ++ (if ex < 10 then "0" else "") ++ show ex
            else
              let decimals = p - 1 - e
                  ds = padded (decimals + 1) n
               in strip (withPoint ds (length ds - decimals))

-- "%-<w>s": left-justify in a field of w characters
leftIn :: Int -> String -> String
leftIn w s = s ++ replicate (w - length s) ' '

main :: IO ()
main = do
  input <- getContents
  mapM_ (\l -> let [(w, _)] = readHex l :: [(Word64, String)]
                   d = castWord64ToDouble w
                   nanSign = if isNaN d && w >= 0x8000000000000000 then "-" else ""
                   nan = nanSign ++ "nan"
                   fmt k = if isNaN d then nan else k d
               in putStrLn (unwords
                    [ fmt (formatG 18), fmt (formatF 8), fmt (formatG 17), fmt (formatG 6)
                    , "[" ++ leftIn 13 (fmt (formatGAlt True 6)) ++ "]" ])) (lines input)
