{-# LANGUAGE ForeignFunctionInterface #-}

-- | The simulated memory (C: src/memory.c).
--
-- Memory is five separate segments:
--
-- >  0x00400000  text         instructions        (fixed size)
-- >  0x10000000  data         .data, then sbrk    (grows up)
-- >  0x7fffffff  stack        down from here      (grows down)
-- >  0x80000000  kernel text  the exception handler
-- >  0x90000000  kernel data
-- >  0xffff0000  memory-mapped IO (with -mapped_io)
--
-- The text segments hold instructions (shared 'InstRef's), not bytes.  The
-- data segments hold bytes, and words are put together little-endian, as on
-- the x86 and ARM machines the C engine runs on.  A bad access raises a
-- MIPS exception (bad address, misaligned), just as on a real MIPS.
module Memory
  ( makeMemory
  , memReadInst
  , memWriteInst
  , memReadByte
  , memReadHalf
  , memReadWord
  , memWriteByte
  , memWriteHalf
  , memWriteWord
  , expandData
  , expandKData
  , memReference
  , readCString
  , checkMemoryMappedIO
  , consoleInputAvailable
  , textBot
  , dataBot
  , stackTop
  , kTextBot
  , kDataBot
  ) where

import Control.Monad (forM_, when)
import Data.Array.IO (IOUArray, getBounds, newArray, readArray, writeArray)
import Data.Bits (complement, shiftL, shiftR, (.&.), (.|.))
import Data.Char (chr)
import Data.Int (Int16, Int32, Int8)
import Data.IORef (newIORef)
import Data.Word (Word32, Word8)
import Foreign.C.Types (CInt (..))
import Foreign.Marshal.Alloc (allocaBytes)
import Foreign.Ptr (Ptr, plusPtr)
import Foreign.Storable (poke)
import Instruction (instDecode)
import Output
import Registers
import State
import Types

foreign import ccall unsafe "poll" c_poll :: Ptr () -> CInt -> CInt -> IO CInt

textBot, dataBot, stackTop, kTextBot, kDataBot, mmIoBot :: Word32
textBot = 0x400000
dataBot = 0x10000000
stackTop = 0x80000000
kTextBot = 0x80000000
kDataBot = 0x90000000
mmIoBot = 0xffff0000

roundUp :: Int -> Int -> Int
roundUp v b = (v + b - 1) .&. complement (b - 1)

-- | Allocate all segments, zeroed (C: @make_memory@).
makeMemory :: Spim -> IO ()
makeMemory s = do
  (textSize, dataSize0, dataLimit, stackSize0, stackLimit, kTextSize, kDataSize0, kDataLimit) <-
    get (initialSizes s)
  let dataSize' = if dataSize0 <= 65536 then 65536 else roundUp dataSize0 4
      stackSize = roundUp stackSize0 4
      kDataSize = roundUp kDataSize0 4
      instSlots n = (n + 3) `quot` 4
  t <- newArray (0, max 0 (instSlots textSize - 1)) Nothing
  set (textSeg s) t
  set (textTop s) (textBot + fromIntegral textSize)
  d <- newArray (0, dataSize' - 1) 0
  set (dataSeg s) d
  set (dataTop s) (dataBot + fromIntegral dataSize')
  set (dataSizeLimit s) dataLimit
  st <- newArray (0, stackSize - 1) 0
  set (stackSeg s) st
  set (stackBot s) (stackTop - fromIntegral stackSize)
  set (stackSizeLimit s) stackLimit
  kt <- newArray (0, max 0 (instSlots kTextSize - 1)) Nothing
  set (kTextSeg s) kt
  set (kTextTop s) (kTextBot + fromIntegral kTextSize)
  kd <- newArray (0, kDataSize - 1) 0
  set (kDataSeg s) kd
  set (kDataTop s) (kDataBot + fromIntegral kDataSize)
  set (kDataSizeLimit s) kDataLimit

-- | Grow a byte segment to a new size, keeping its contents at the start.
growBytes :: IOUArray Int Word8 -> Int -> IO (IOUArray Int Word8)
growBytes old newSize = do
  (_, hi) <- getBounds old
  new <- newArray (0, newSize - 1) 0
  forM_ [0 .. min hi (newSize - 1)] $ \i -> readArray old i >>= writeArray new i
  pure new

-- | Grow the data segment by n bytes (C: @expand_data@), for sbrk and for
-- data directives that run off the end.
expandData :: Spim -> Int -> IO ()
expandData s addl = do
  top <- get (dataTop s)
  limit <- get (dataSizeLimit s)
  let delta = roundUp addl 4
      oldSize = fromIntegral (top - dataBot) :: Int
      newSize = fromIntegral (fromIntegral (oldSize + delta) :: Int32) :: Int
  when (addl < 0 || newSize > limit) $ do
    cError ("Can't expand data segment by " ++ show addl ++ " bytes to " ++ show newSize ++ " bytes\n")
    runError ("Use -ldata # with # > " ++ show newSize ++ "\n")
  seg <- get (dataSeg s)
  new <- growBytes seg newSize
  set (dataSeg s) new
  set (dataTop s) (top + fromIntegral delta)

-- | Grow the stack down (C: @expand_stack@): at least double it.
expandStack :: Spim -> Int -> IO ()
expandStack s addl = do
  bot <- get (stackBot s)
  limit <- get (stackSizeLimit s)
  let delta = roundUp addl 4
      oldSize = fromIntegral (stackTop - bot) :: Int
      newSize = oldSize + max delta oldSize
  when (addl < 0 || newSize > limit) $
    runError
      ( "Can't expand stack segment by " ++ show addl ++ " bytes to " ++ show newSize
          ++ " bytes.\nUse -lstack # with # > " ++ show newSize ++ "\n"
      )
  old <- get (stackSeg s)
  new <- newArray (0, newSize - 1) 0
  let shift = newSize - oldSize
  forM_ [0 .. oldSize - 1] $ \i -> readArray old i >>= writeArray new (i + shift)
  set (stackSeg s) new
  set (stackBot s) (bot - fromIntegral shift)

-- | Grow the kernel data segment (C: @expand_k_data@).
expandKData :: Spim -> Int -> IO ()
expandKData s addl = do
  top <- get (kDataTop s)
  limit <- get (kDataSizeLimit s)
  let delta = roundUp addl 4
      oldSize = fromIntegral (top - kDataBot) :: Int
      newSize = oldSize + delta
  when (addl < 0 || newSize > limit) $
    runError
      ( "Can't expand kernel data segment by " ++ show addl ++ " bytes to " ++ show newSize
          ++ " bytes.\nUse -lkdata # with # > " ++ show newSize ++ "\n"
      )
  seg <- get (kDataSeg s)
  new <- growBytes seg newSize
  set (kDataSeg s) new
  set (kDataTop s) (top + fromIntegral delta)

-- | Which byte segment holds an address: the bytes and the offset in them.
byteSegment :: Spim -> Word32 -> IO (Maybe (IOUArray Int Word8, Int))
byteSegment s addr = do
  dtop <- get (dataTop s)
  sbot <- get (stackBot s)
  kdtop <- get (kDataTop s)
  if addr >= dataBot && addr < dtop
    then (\a -> Just (a, fromIntegral (addr - dataBot))) <$> get (dataSeg s)
    else
      if addr >= sbot && addr < stackTop
        then (\a -> Just (a, fromIntegral (addr - sbot))) <$> get (stackSeg s)
        else
          if addr >= kDataBot && addr < kdtop
            then (\a -> Just (a, fromIntegral (addr - kDataBot))) <$> get (kDataSeg s)
            else pure Nothing

-- | The bytes behind an address, for the system calls that hand memory to
-- the host (C: @mem_reference@).  Text addresses give Nothing (the C engine
-- points into its table of instruction pointers there).  An address in no
-- segment is a run-time error.
memReference :: Spim -> Word32 -> IO (Maybe (IOUArray Int Word8, Int))
memReference s addr = do
  ttop <- get (textTop s)
  kttop <- get (kTextTop s)
  seg <- byteSegment s addr
  case seg of
    Just r -> pure (Just r)
    Nothing
      | (addr >= textBot && addr < ttop) || (addr >= kTextBot && addr < kttop) -> pure Nothing
      | otherwise -> runError "Memory address out of bounds\n"

-- | The NUL-terminated string at an address (C: @(char *)mem_reference(a)@
-- printed with @%s@).  It stops at the end of the segment.
readCString :: Spim -> Word32 -> IO String
readCString s addr = do
  ref <- memReference s addr
  case ref of
    Nothing -> pure ""
    Just (arr, off) -> do
      (_, hi) <- getBounds arr
      let go i acc
            | i > hi = pure (reverse acc)
            | otherwise = do
                b <- readArray arr i
                if b == 0 then pure (reverse acc) else go (i + 1) (chr (fromIntegral b) : acc)
      go off []

-- | The instruction at an address (C: @mem_read_inst@).  A bad address
-- raises a bus error and gives the decoding of 0.
memReadInst :: Spim -> Word32 -> IO (Maybe InstRef)
memReadInst s addr = do
  ttop <- get (textTop s)
  kttop <- get (kTextTop s)
  if addr >= textBot && addr < ttop && addr .&. 3 == 0
    then get (textSeg s) >>= \a -> readArray a (fromIntegral ((addr - textBot) `shiftR` 2))
    else
      if addr >= kTextBot && addr < kttop && addr .&. 3 == 0
        then get (kTextSeg s) >>= \a -> readArray a (fromIntegral ((addr - kTextBot) `shiftR` 2))
        else do
          raiseException s excIBE
          setCp0 s cp0BadVAddr (fromIntegral addr)
          Just <$> newIORef (instDecode 0)

-- | Store an instruction (C: @mem_write_inst@).
memWriteInst :: Spim -> Word32 -> Maybe InstRef -> IO ()
memWriteInst s addr inst = do
  ttop <- get (textTop s)
  kttop <- get (kTextTop s)
  if addr >= textBot && addr < ttop && addr .&. 3 == 0
    then get (textSeg s) >>= \a -> writeArray a (fromIntegral ((addr - textBot) `shiftR` 2)) inst
    else
      if addr >= kTextBot && addr < kttop && addr .&. 3 == 0
        then get (kTextSeg s) >>= \a -> writeArray a (fromIntegral ((addr - kTextBot) `shiftR` 2)) inst
        else do
          raiseException s excIBE
          setCp0 s cp0BadVAddr (fromIntegral addr)
          enc <- maybe (pure 0) (fmap inEncoding . get) inst
          memWriteWord s addr enc

readBytes :: IOUArray Int Word8 -> Int -> Int -> IO Word32
readBytes arr off n =
  foldr (\i acc -> do
            b <- readArray arr (off + i)
            rest <- acc
            pure (rest .|. (fromIntegral b `shiftL` (8 * i))))
    (pure 0) [0 .. n - 1]

writeBytes :: IOUArray Int Word8 -> Int -> Int -> Word32 -> IO ()
writeBytes arr off n v = forM_ [0 .. n - 1] $ \i -> writeArray arr (off + i) (fromIntegral (v `shiftR` (8 * i)))

-- | Load a byte, sign-extended (C: @mem_read_byte@).
memReadByte :: Spim -> Word32 -> IO Int32
memReadByte s addr = do
  seg <- byteSegment s addr
  case seg of
    Just (arr, off) -> fromIntegral . (fromIntegral :: Word8 -> Int8') <$> readArray arr off
    Nothing -> badMemRead s addr 0

-- | Load a half word, sign-extended (C: @mem_read_half@).
memReadHalf :: Spim -> Word32 -> IO Int32
memReadHalf s addr = do
  seg <- byteSegment s addr
  case seg of
    Just (arr, off) | addr .&. 1 == 0 -> fromIntegral . (fromIntegral :: Word32 -> Int16') <$> readBytes arr off 2
    _ -> badMemRead s addr 1

-- | Load a word (C: @mem_read_word@).
memReadWord :: Spim -> Word32 -> IO Int32
memReadWord s addr = do
  seg <- byteSegment s addr
  case seg of
    Just (arr, off) | addr .&. 3 == 0 -> fromIntegral <$> readBytes arr off 4
    _ -> badMemRead s addr 3

type Int8' = Int8

type Int16' = Int16

memWriteByte :: Spim -> Word32 -> Int32 -> IO ()
memWriteByte s addr v = do
  seg <- byteSegment s addr
  case seg of
    Just (arr, off) -> writeArray arr off (fromIntegral v)
    Nothing -> badMemWrite s addr v 0

memWriteHalf :: Spim -> Word32 -> Int32 -> IO ()
memWriteHalf s addr v = do
  seg <- byteSegment s addr
  case seg of
    Just (arr, off) | addr .&. 1 == 0 -> writeBytes arr off 2 (fromIntegral v)
    _ -> badMemWrite s addr v 1

memWriteWord :: Spim -> Word32 -> Int32 -> IO ()
memWriteWord s addr v = do
  seg <- byteSegment s addr
  case seg of
    Just (arr, off) | addr .&. 3 == 0 -> writeBytes arr off 4 (fromIntegral v)
    _ -> badMemWrite s addr v 3

-- | Is an address just below the stack, so that touching it should grow
-- the stack?  (More than 16 MB below it is probably a bad pointer.)
belowStack :: Spim -> Word32 -> IO Bool
belowStack s addr = do
  dtop <- get (dataTop s)
  sbot <- get (stackBot s)
  pure (addr > dtop && addr < sbot && addr > sbot - fromIntegral (16 * mega))

-- | The unusual and erroneous loads (C: @bad_mem_read@): misaligned,
-- reading the text segment as data, growing the stack, memory-mapped IO,
-- or no memory at all.
badMemRead :: Spim -> Word32 -> Word32 -> IO Int32
badMemRead s addr mask = do
  ttop <- get (textTop s)
  grow <- belowStack s addr
  if addr .&. mask /= 0
    then raiseAt excAdEL >> pure 0
    else
      if addr >= textBot && addr < ttop
        then do
          slot <- get (textSeg s) >>= \a -> readArray a (fromIntegral ((addr - textBot) `shiftR` 2))
          enc <- maybe (pure 0) (fmap (fromIntegral . inEncoding) . get) slot :: IO Word32
          pure $ case mask of
            0 -> fromIntegral (0xff .&. (enc `shiftR` (8 * fromIntegral (addr .&. 3))))
            1 -> fromIntegral (0xffff .&. (enc `shiftR` (8 * fromIntegral (addr .&. 2))))
            _ -> fromIntegral enc
        else
          if grow
            then do
              sbot <- get (stackBot s)
              expandStack s (fromIntegral (sbot - addr + 4))
              pure 0
            else
              if addr >= mmIoBot
                then readMemoryMappedIO s addr
                else raiseAt excDBE >> pure 0
  where
    raiseAt code = raiseException s code >> setCp0 s cp0BadVAddr (fromIntegral addr)

-- | The unusual and erroneous stores (C: @bad_mem_write@).  A store into
-- the text segment re-decodes the changed word into an instruction.
badMemWrite :: Spim -> Word32 -> Int32 -> Word32 -> IO ()
badMemWrite s addr value mask = do
  ttop <- get (textTop s)
  grow <- belowStack s addr
  if addr .&. mask /= 0
    then raiseAt excAdES
    else
      if addr >= textBot && addr < ttop
        then do
          arr <- get (textSeg s)
          let idx = fromIntegral ((addr - textBot) `shiftR` 2)
          slot <- readArray arr idx
          new <- case slot of
            Nothing -> pure (if mask == 3 then value else 0)
            Just ref -> do
              old <- fromIntegral . inEncoding <$> get ref :: IO Word32
              let v = fromIntegral value :: Word32
                  sh n = 8 * fromIntegral (addr .&. n)
              pure $ fromIntegral $ case mask of
                0 -> (old .&. complement (0xff `shiftL` sh 3)) .|. ((v .&. 0xff) `shiftL` sh 3)
                1 -> (old .&. complement (0xffff `shiftL` sh 2)) .|. ((v .&. 0xffff) `shiftL` sh 2)
                _ -> v
          ref <- newIORef (instDecode new)
          writeArray arr idx (Just ref)
        else
          if grow
            then do
              sbot <- get (stackBot s)
              expandStack s (fromIntegral (sbot - addr + 4))
              sbot' <- get (stackBot s)
              if addr >= sbot'
                then do
                  seg <- get (stackSeg s)
                  let off = fromIntegral (addr - sbot')
                  case mask of
                    0 -> writeArray seg off (fromIntegral value)
                    1 -> writeBytes seg off 2 (fromIntegral value)
                    _ -> writeBytes seg off 4 (fromIntegral value)
                else raiseAt excDBE
            else
              if addr >= mmIoBot
                then writeMemoryMappedIO s addr value
                else raiseAt excDBE
  where
    raiseAt code = raiseException s code >> setCp0 s cp0BadVAddr (fromIntegral addr)

-- The memory-mapped console: a receiver (keyboard) and a transmitter
-- (display), each with a control word and a data word.
recvCtrlAddr, recvBufferAddr, transCtrlAddr, transBufferAddr :: Word32
recvCtrlAddr = 0xffff0000
recvBufferAddr = 0xffff0004
transCtrlAddr = 0xffff0008
transBufferAddr = 0xffff000c

recvReady, recvIntEnable, transReady, transIntEnable :: Word32
recvReady = 1
recvIntEnable = 2
transReady = 1
transIntEnable = 2

-- | Set or clear a pending-interrupt bit in the Cause register.
raiseInterrupt, clearInterrupt :: Spim -> Int -> IO ()
raiseInterrupt s level = do
  c <- getCp0 s cp0Cause
  setCp0 s cp0Cause (c .|. (1 `shiftL` (level + 8)))
clearInterrupt s level = do
  c <- getCp0 s cp0Cause
  setCp0 s cp0Cause (c .&. complement (1 `shiftL` (level + 8)))

-- | Is a byte waiting on standard input?  (Only with -mapped_io.)
consoleInputAvailable :: Spim -> IO Bool
consoleInputAvailable s = do
  mio <- get (optMappedIo s)
  if not mio
    then pure False
    else allocaBytes 8 $ \p -> do
      poke (p :: Ptr CInt) 0
      poke (p `plusPtr` 4 :: Ptr Int16') 1
      poke (p `plusPtr` 6 :: Ptr Int16') 0
      n <- c_poll (castP p) 1 0
      pure (n > 0)
  where
    castP :: Ptr CInt -> Ptr ()
    castP p = p `plusPtr` 0

-- | Every IO interval: take a waiting input byte, finish an output byte
-- (C: @check_memory_mapped_IO@).
checkMemoryMappedIO :: Spim -> IO ()
checkMemoryMappedIO s = do
  rt <- get (recvTimer s)
  if rt > 0
    then set (recvTimer s) (rt - 1)
    else do
      avail <- consoleInputAvailable s
      when avail $ do
        b <- readByte
        let c = maybe 0 fromIntegral b :: Int
        when (c == 3) $ do
          writeErr "\nExecution interrupted\n"
          runErrorQuiet
        set (recvBuffer s) c
        modify (recvControl s) (.|. recvReady)
        set (recvTimer s) 100
        rc <- get (recvControl s)
        when (rc .&. recvIntEnable /= 0) $ raiseInterrupt s 3
  tt <- get (transTimer s)
  if tt > 0
    then set (transTimer s) (tt - 1)
    else do
      tc <- get (transControl s)
      when (tc .&. transReady == 0) $ do
        set (transControl s) (tc .|. transReady)
        when (tc .&. transIntEnable /= 0) $ raiseInterrupt s 2
  where
    runErrorQuiet = runError ""

writeMemoryMappedIO :: Spim -> Word32 -> Int32 -> IO ()
writeMemoryMappedIO s addr value
  | addr == transCtrlAddr =
      if v .&. transIntEnable /= 0
        then do
          modify (transControl s) (.|. transIntEnable)
          tc <- get (transControl s)
          when (tc .&. transReady /= 0) $ raiseInterrupt s 2
        else do
          modify (transControl s) (.&. complement transIntEnable)
          clearInterrupt s 2
  | addr == transBufferAddr = do
      tc <- get (transControl s)
      when (tc .&. transReady /= 0) $ do
        let c = fromIntegral (value .&. 0xff)
        set (transBuffer s) c
        writeOut [chr c]
        set (transControl s) (tc .&. complement transReady)
        set (transTimer s) 100
        clearInterrupt s 2
  | addr == recvCtrlAddr =
      if v .&. recvIntEnable /= 0
        then do
          modify (recvControl s) (.|. recvIntEnable)
          rc <- get (recvControl s)
          when (rc .&. recvReady /= 0) $ raiseInterrupt s 3
        else do
          modify (recvControl s) (.&. complement recvIntEnable)
          clearInterrupt s 3
  | addr == recvBufferAddr = pure ()
  | otherwise = runError ("Write to unused memory-mapped IO address (0x" ++ hex addr ++ ")\n")
  where
    v = fromIntegral value :: Word32

readMemoryMappedIO :: Spim -> Word32 -> IO Int32
readMemoryMappedIO s addr
  | addr == transCtrlAddr = fromIntegral <$> get (transControl s)
  | addr == transBufferAddr = fromIntegral . (.&. 0xff) <$> get (transBuffer s)
  | addr == recvCtrlAddr = fromIntegral <$> get (recvControl s)
  | addr == recvBufferAddr = do
      modify (recvControl s) (.&. complement recvReady)
      set (recvTimer s) 0
      clearInterrupt s 3
      fromIntegral . (.&. 0xff) <$> get (recvBuffer s)
  | otherwise = runError ("Read from unused memory-mapped IO address (0x" ++ hex addr ++ ")\n")

hex :: Word32 -> String
hex 0 = "0"
hex n = go n ""
  where
    go 0 acc = acc
    go k acc = go (k `quot` 16) ("0123456789abcdef" !! fromIntegral (k `rem` 16) : acc)
