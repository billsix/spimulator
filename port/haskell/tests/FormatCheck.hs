-- | Print each double (read as a 64-bit hex pattern, one per line) in the
-- five formats the C engine uses, with the port's formatter, in the same
-- layout as cfmt.c prints them with glibc's printf.  check-format.sh
-- compares the two.
module Main (main) where

import CFormat (formatF, formatG, formatGAlt, leftIn)
import Data.Word (Word64)
import GHC.Float (castWord64ToDouble)
import Numeric (readHex)

main :: IO ()
main = do
  input <- getContents
  mapM_ (putStrLn . formatLine) (lines input)

formatLine :: String -> String
formatLine l =
  let d = case readHex l :: [(Word64, String)] of
        ((w, _) : _) -> castWord64ToDouble w
        [] -> 0
   in unwords
        [formatG 18 d, formatF 8 d, formatG 17 d, formatG 6 d, "[" ++ leftIn 13 (formatGAlt True 6 d) ++ "]"]
