{-# LANGUAGE ForeignFunctionInterface #-}

-- | Writing to the console and reporting errors (C: the IO part of spim.c).
--
-- Text in the engine is a 'String' whose characters are bytes (0..255): the
-- C engine handles bytes, not Unicode, and so does this port.  Output goes
-- out byte for byte, and is flushed after every write, as the C engine's
-- @write_output@ does, so that it interleaves with the program's own
-- @write@ system calls in the same order.
--
-- Console input is read straight from file descriptor 0 one byte at a
-- time, with the C library's @read@, exactly as the C engine does; that
-- way no input is held in a buffer where a later reader could not see it.
module Output
  ( SpimJump (..)
  , writeOut
  , writeErr
  , cError
  , runError
  , fatalError
  , exitSpim
  , readByte
  , setupHandles
  , cRead
  , cWrite
  , cOpen
  , cClose
  ) where

import Control.Exception (Exception, throwIO)
import qualified Data.ByteString.Char8 as B
import Data.Word (Word8)
import Foreign.C.String (CString)
import Foreign.C.Types (CInt (..), CSize (..))
import Foreign.Marshal.Alloc (alloca)
import Foreign.Ptr (Ptr)
import Foreign.Storable (peek)
import System.Exit (ExitCode (..), exitWith)
import System.IO (BufferMode (..), Handle, hFlush, hSetBinaryMode, hSetBuffering, stderr, stdout, stdin)

foreign import ccall safe "read" c_read :: CInt -> Ptr Word8 -> CSize -> IO CInt
foreign import ccall safe "write" c_write :: CInt -> Ptr Word8 -> CSize -> IO CInt
foreign import ccall safe "open" c_open :: CString -> CInt -> CInt -> IO CInt
foreign import ccall safe "close" c_close :: CInt -> IO CInt

-- | The C library's @read@, @write@, @open@ and @close@, for the
-- program's file system calls.
cRead :: CInt -> Ptr Word8 -> CSize -> IO CInt
cRead = c_read

cWrite :: CInt -> Ptr Word8 -> CSize -> IO CInt
cWrite = c_write

cOpen :: CString -> CInt -> CInt -> IO CInt
cOpen = c_open

cClose :: CInt -> IO CInt
cClose = c_close

-- | The C engine leaves the top level with @longjmp@ after a run-time
-- error; this port throws 'RunErrorJump' and catches it where C calls
-- @setjmp@.
data SpimJump = RunErrorJump
  deriving (Show)

instance Exception SpimJump

-- | Make the standard handles carry bytes, not Unicode.
setupHandles :: IO ()
setupHandles = do
  mapM_ (`hSetBinaryMode` True) [stdin, stdout, stderr]
  hSetBuffering stdout (BlockBuffering Nothing)
  hSetBuffering stderr NoBuffering

put :: Handle -> String -> IO ()
put h text = B.hPut h (B.pack text) >> hFlush h

-- | Write to the program's console (C: @write_output(console_out, ...)@).
writeOut :: String -> IO ()
writeOut = put stdout

-- | Write a message to stderr (C: @write_output(message_out, ...)@).
writeErr :: String -> IO ()
writeErr = put stderr

-- | C's @error@: a message on stderr.
cError :: String -> IO ()
cError = writeErr

-- | C's @run_error@: print the message, then return to the top level.
runError :: String -> IO a
runError msg = writeErr msg >> throwIO RunErrorJump

-- | C's @fatal_error@: print the message and exit with status 255
-- (C's @exit(-1)@).
fatalError :: String -> IO a
fatalError msg = writeErr msg >> exitSpim 255

-- | Exit the process with a C-style status (only the low 8 bits count).
exitSpim :: Int -> IO a
exitSpim code = do
  hFlush stdout
  let low = code `mod` 256
  exitWith (if low == 0 then ExitSuccess else ExitFailure low)

-- | One byte from standard input, or Nothing at end of file (C: @read(0,
-- &c, 1)@).
readByte :: IO (Maybe Word8)
readByte = alloca $ \p -> do
  n <- c_read 0 p 1
  if n <= 0 then pure Nothing else Just <$> peek p
