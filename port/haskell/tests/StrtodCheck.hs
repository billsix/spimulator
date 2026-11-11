-- | Print the bits of cStrtod of each input line, in hex, the same way
-- strtod.c prints the C library's atof; check-strtod.sh compares them.
module Main (main) where

import CFormat (cStrtod)
import GHC.Float (castDoubleToWord64)
import Numeric (showHex)

main :: IO ()
main = getContents >>= mapM_ (\l -> putStrLn (showHex (castDoubleToWord64 (cStrtod l)) "")) . lines
