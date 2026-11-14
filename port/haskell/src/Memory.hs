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
  ( makeMemory,
    memoryReadInstruction,
    memoryPeekInstruction,
    memoryWriteInstruction,
    memoryReadByte,
    memoryReadHalf,
    memoryReadWord,
    memoryWriteByte,
    memoryWriteHalf,
    memoryWriteWord,
    expandData,
    expandKernelData,
    memoryReference,
    SegmentRef,
    readCString,
    checkMemoryMappedIO,
    consoleInputAvailable,
    textBottom,
    dataBottom,
    stackTop,
    kernelTextBottom,
    kernelDataBottom,
  )
where

import Control.Monad (forM_, when)
import Data.Array.IO (IOArray, IOUArray, getBounds, newArray, readArray, writeArray)
import Data.Bits (complement, shiftL, shiftR, (.&.), (.|.))
import Data.Char (chr)
import Data.IORef (IORef, newIORef)
import Data.Int (Int16, Int32, Int8)
import Data.Word (Word32, Word8)
import Foreign.C.Types (CInt (..))
import Foreign.Marshal.Alloc (allocaBytes)
import Foreign.Ptr (Ptr, plusPtr)
import Foreign.Storable (poke)
import Instruction (instructionDecode)
import Numeric (showHex)
import Output
import Registers
import State
import Types

foreign import ccall unsafe "poll" c_poll :: Ptr () -> CInt -> CInt -> IO CInt

textBottom, dataBottom, stackTop, kernelTextBottom, kernelDataBottom, memoryMappedIOBottom :: Word32
textBottom = 0x400000
dataBottom = 0x10000000
stackTop = 0x80000000
kernelTextBottom = 0x80000000
kernelDataBottom = 0x90000000
memoryMappedIOBottom = 0xffff0000

roundUp :: Int -> Int -> Int
roundUp v b = (v + b - 1) .&. complement (b - 1)

-- | Allocate all segments, zeroed (C: @make_memory@).
makeMemory :: Spim -> IO ()
makeMemory s = do
  (textSize, dataSize0, dataLimit, stackSize0, stackLimit, kernelTextSize, kernelDataSize0, kernelDataLimit) :: (Int, Int, Int, Int, Int, Int, Int, Int) <-
    get (initialSizes s)
  -- A text segment that can hold no instruction, or a kernel text segment
  -- that stops short of the exception vector, would make every exception
  -- jump outside its segment, and the run would loop.
  when (textSize < 4) $
    fatalError ("-stext " ++ show textSize ++ ": the text segment needs at least one word (4 bytes)\n")
  when (kernelTextSize < 0x180 + 4) $
    fatalError
      ( "-sktext "
          ++ show kernelTextSize
          ++ ": the kernel text segment must hold the exception handler at 0x80000180 (at least 388 bytes)\n"
      )
  let dataSize' :: Int
      dataSize' = if dataSize0 <= 65536 then 65536 else roundUp dataSize0 4
      stackSize :: Int
      stackSize = roundUp stackSize0 4
      kernelDataSize :: Int
      kernelDataSize = roundUp kernelDataSize0 4
      instructionSlots :: Int -> Int
      instructionSlots n = (n + 3) `quot` 4
  t :: IOArray Int (Maybe InstructionRef) <- newArray (0, max 0 (instructionSlots textSize - 1)) Nothing
  set (textSegment s) t
  set (textTop s) (textBottom + fromIntegral textSize)
  d :: IOUArray Int Word8 <- newArray (0, dataSize' - 1) 0
  set (dataSegment s) d
  set (dataTop s) (dataBottom + fromIntegral dataSize')
  set (dataSizeLimit s) dataLimit
  st :: IOUArray Int Word8 <- newArray (0, stackSize - 1) 0
  set (stackSegment s) st
  set (stackBottom s) (stackTop - fromIntegral stackSize)
  set (stackSizeLimit s) stackLimit
  kt :: IOArray Int (Maybe InstructionRef) <- newArray (0, max 0 (instructionSlots kernelTextSize - 1)) Nothing
  set (kernelTextSegment s) kt
  set (kernelTextTop s) (kernelTextBottom + fromIntegral kernelTextSize)
  kd :: IOUArray Int Word8 <- newArray (0, kernelDataSize - 1) 0
  set (kernelDataSegment s) kd
  set (kernelDataTop s) (kernelDataBottom + fromIntegral kernelDataSize)
  set (kernelDataSizeLimit s) kernelDataLimit

-- | Wrap a size to a C int, as C's int arithmetic does on this host.
wrap32 :: Int -> Int
wrap32 n = fromIntegral (fromIntegral n :: Int32)

-- | Grow a byte segment to a new size, keeping its contents at the start.
growBytes :: IOUArray Int Word8 -> Int -> IO (IOUArray Int Word8)
growBytes old newSize = do
  (_, hi) :: (Int, Int) <- getBounds old
  new :: IOUArray Int Word8 <- newArray (0, newSize - 1) 0
  forM_ [0 .. min hi (newSize - 1)] $ \i -> readArray old i >>= writeArray new i
  pure new

-- | Grow the data segment by n bytes (C: @expand_data@), for sbrk and for
-- data directives that run off the end.
expandData :: Spim -> Int -> IO ()
expandData s =
  growSegment
    UpwardSegment
      { upBottom = dataBottom,
        upTop = dataTop s,
        upLimit = dataSizeLimit s,
        upBytes = dataSegment s,
        upTooBig = \additional newSize -> do
          cError ("Can't expand data segment by " ++ show additional ++ " bytes to " ++ show newSize ++ " bytes\n")
          runError ("Use -ldata # with # > " ++ show newSize ++ "\n"),
        upReallocFailed = "realloc failed in expand_data\n"
      }

-- | A segment that grows up from a fixed bottom: the data and kernel data
-- segments.
data UpwardSegment = UpwardSegment
  { upBottom :: Word32,
    upTop :: IORef Word32,
    upLimit :: IORef Int,
    upBytes :: IORef (IOUArray Int Word8),
    -- | The report of a growth by n bytes to a size past the limit.
    upTooBig :: Int -> Int -> IO (),
    -- | The fatal error for a size that wrapped negative.
    upReallocFailed :: String
  }

-- | Grow an upward segment by n bytes (the body of C's @expand_data@ and
-- @expand_k_data@).
growSegment :: UpwardSegment -> Int -> IO ()
growSegment segment additional = do
  top :: Word32 <- get (upTop segment)
  limit :: Int <- get (upLimit segment)
  -- The sizes are C ints: a growth that wraps negative passes the limit check,
  -- and C's realloc of that size then fails.
  let delta :: Int
      delta = wrap32 (roundUp additional 4)
      oldSize :: Int
      oldSize = fromIntegral (top - upBottom segment) :: Int
      newSize :: Int
      newSize = wrap32 (oldSize + delta)
  when (additional < 0 || newSize > limit) $ upTooBig segment additional newSize
  when (newSize < 0) $ fatalError (upReallocFailed segment)
  bytes :: IOUArray Int Word8 <- get (upBytes segment)
  (_, hi) :: (Int, Int) <- getBounds bytes
  -- The array keeps spare capacity, so a run of small sbrk calls copies the
  -- segment only when the capacity runs out, not on every call.  Bytes past
  -- the top are unreachable (byteSegment stops at it), so they stay zero.
  when (newSize > hi + 1) $ do
    new :: IOUArray Int Word8 <- growBytes bytes (max newSize (min limit (2 * (hi + 1))))
    set (upBytes segment) new
  set (upTop segment) (top + fromIntegral delta)

-- | Grow the stack down (C: @expand_stack@): at least double it.
expandStack :: Spim -> Int -> IO ()
expandStack s additional = do
  bottom :: Word32 <- get (stackBottom s)
  limit :: Int <- get (stackSizeLimit s)
  -- The sizes are C ints, as in 'growSegment'; C hands a negative size to
  -- xmalloc as a size_t.
  let delta :: Int
      delta = wrap32 (roundUp additional 4)
      oldSize :: Int
      oldSize = fromIntegral (stackTop - bottom) :: Int
      newSize :: Int
      newSize = wrap32 (oldSize + max delta oldSize)
  when (additional < 0 || newSize > limit) $
    runError
      ( "Can't expand stack segment by "
          ++ show additional
          ++ " bytes to "
          ++ show newSize
          ++ " bytes.\nUse -lstack # with # > "
          ++ show newSize
          ++ "\n"
      )
  when (newSize < 0) $
    fatalError ("Out of memory at request for " ++ show (toInteger newSize `mod` (2 ^ (64 :: Int))) ++ " bytes.\n")
  old :: IOUArray Int Word8 <- get (stackSegment s)
  new :: IOUArray Int Word8 <- newArray (0, newSize - 1) 0
  let shift :: Int
      shift = newSize - oldSize
  forM_ [0 .. oldSize - 1] $ \i -> readArray old i >>= writeArray new (i + shift)
  set (stackSegment s) new
  set (stackBottom s) (bottom - fromIntegral shift)

-- | Grow the kernel data segment (C: @expand_k_data@).
expandKernelData :: Spim -> Int -> IO ()
expandKernelData s =
  growSegment
    UpwardSegment
      { upBottom = kernelDataBottom,
        upTop = kernelDataTop s,
        upLimit = kernelDataSizeLimit s,
        upBytes = kernelDataSegment s,
        upTooBig = \additional newSize ->
          runError
            ( "Can't expand kernel data segment by "
                ++ show additional
                ++ " bytes to "
                ++ show newSize
                ++ " bytes.\nUse -lkdata # with # > "
                ++ show newSize
                ++ "\n"
            ),
        upReallocFailed = "realloc failed in expand_k_data\n"
      }

-- | A place in a byte segment: the segment's bytes, the offset of the address
-- in them, and the segment's last valid offset.  The array can be longer than
-- the segment (the data segment grows with spare capacity), so the last
-- offset, not the array's bounds, is where the segment ends.
type SegmentRef = (IOUArray Int Word8, Int, Int)

-- | Which byte segment holds an address.
byteSegment :: Spim -> Word32 -> IO (Maybe SegmentRef)
byteSegment s address = do
  dtop :: Word32 <- get (dataTop s)
  sbot :: Word32 <- get (stackBottom s)
  kdtop :: Word32 <- get (kernelDataTop s)
  if address >= dataBottom && address < dtop
    then (\a -> Just (a, fromIntegral (address - dataBottom), fromIntegral (dtop - dataBottom) - 1)) <$> get (dataSegment s)
    else
      if address >= sbot && address < stackTop
        then (\a -> Just (a, fromIntegral (address - sbot), fromIntegral (stackTop - sbot) - 1)) <$> get (stackSegment s)
        else
          if address >= kernelDataBottom && address < kdtop
            then (\a -> Just (a, fromIntegral (address - kernelDataBottom), fromIntegral (kdtop - kernelDataBottom) - 1)) <$> get (kernelDataSegment s)
            else pure Nothing

-- | The bytes behind an address, for the system calls that hand memory to
-- the host (C: @mem_reference@).  Text addresses give Nothing (the C engine
-- points into its table of instruction pointers there).  An address in no
-- segment is a run-time error.
memoryReference :: Spim -> Word32 -> IO (Maybe SegmentRef)
memoryReference s address = do
  ttop :: Word32 <- get (textTop s)
  kttop :: Word32 <- get (kernelTextTop s)
  segment :: Maybe SegmentRef <- byteSegment s address
  case segment of
    Just r -> pure (Just r)
    Nothing
      | (address >= textBottom && address < ttop) || (address >= kernelTextBottom && address < kttop) -> pure Nothing
      | otherwise -> runError "Memory address out of bounds\n"

-- | The NUL-terminated string at an address (C: @(char *)mem_reference(a)@
-- printed with @%s@).  It stops at the end of the segment.
readCString :: Spim -> Word32 -> IO String
readCString s address = do
  ref :: Maybe SegmentRef <- memoryReference s address
  case ref of
    Nothing -> pure ""
    Just (array, offset, hi) -> do
      let go :: Int -> String -> IO String
          go i acc
            | i > hi = pure (reverse acc)
            | otherwise = do
                b :: Word8 <- readArray array i
                if b == 0 then pure (reverse acc) else go (i + 1) (chr (fromIntegral b) : acc)
      go offset []

-- | The instruction at an address (C: @mem_read_inst@).  A bad address
-- raises a bus error and gives the decoding of 0.
memoryReadInstruction :: Spim -> Word32 -> IO (Maybe InstructionRef)
memoryReadInstruction s address = do
  ttop :: Word32 <- get (textTop s)
  kttop :: Word32 <- get (kernelTextTop s)
  if address >= textBottom && address < ttop && address .&. 3 == 0
    then get (textSegment s) >>= \a -> readArray a (fromIntegral ((address - textBottom) `shiftR` 2))
    else
      if address >= kernelTextBottom && address < kttop && address .&. 3 == 0
        then get (kernelTextSegment s) >>= \a -> readArray a (fromIntegral ((address - kernelTextBottom) `shiftR` 2))
        else do
          raiseException s exceptionInstructionBusError
          setCoprocessor0 s coprocessor0BadVirtualAddress (fromIntegral address)
          Just <$> newIORef (instructionDecode 0)

-- | The instruction at an address, read without side effects: no
-- exception is raised and nothing changes.  'Nothing' when the address is
-- not an aligned location in a text segment (C: @mem_peek_inst@).
memoryPeekInstruction :: Spim -> Word32 -> IO (Maybe (Maybe InstructionRef))
memoryPeekInstruction s address = do
  ttop :: Word32 <- get (textTop s)
  kttop :: Word32 <- get (kernelTextTop s)
  if address .&. 3 /= 0
    then pure Nothing
    else
      if address >= textBottom && address < ttop
        then Just <$> (get (textSegment s) >>= \a -> readArray a (fromIntegral ((address - textBottom) `shiftR` 2)))
        else
          if address >= kernelTextBottom && address < kttop
            then Just <$> (get (kernelTextSegment s) >>= \a -> readArray a (fromIntegral ((address - kernelTextBottom) `shiftR` 2)))
            else pure Nothing

-- | Store an instruction (C: @mem_write_inst@).
memoryWriteInstruction :: Spim -> Word32 -> Maybe InstructionRef -> IO ()
memoryWriteInstruction s address instruction = do
  ttop :: Word32 <- get (textTop s)
  kttop :: Word32 <- get (kernelTextTop s)
  if address >= textBottom && address < ttop && address .&. 3 == 0
    then get (textSegment s) >>= \a -> writeArray a (fromIntegral ((address - textBottom) `shiftR` 2)) instruction
    else
      if address >= kernelTextBottom && address < kttop && address .&. 3 == 0
        then get (kernelTextSegment s) >>= \a -> writeArray a (fromIntegral ((address - kernelTextBottom) `shiftR` 2)) instruction
        else do
          raiseException s exceptionInstructionBusError
          setCoprocessor0 s coprocessor0BadVirtualAddress (fromIntegral address)
          encoding :: Int32 <- maybe (pure 0) (fmap instructionEncoding . get) instruction
          memoryWriteWord s address encoding

readBytes :: IOUArray Int Word8 -> Int -> Int -> IO Word32
readBytes array offset n =
  foldr
    ( \i acc -> do
        b :: Word8 <- readArray array (offset + i)
        rest :: Word32 <- acc
        pure (rest .|. (fromIntegral b `shiftL` (8 * i)))
    )
    (pure 0)
    [0 .. n - 1]

writeBytes :: IOUArray Int Word8 -> Int -> Int -> Word32 -> IO ()
writeBytes array offset n v = forM_ [0 .. n - 1] $ \i -> writeArray array (offset + i) (fromIntegral (v `shiftR` (8 * i)))

-- | Load a byte, sign-extended (C: @mem_read_byte@).
memoryReadByte :: Spim -> Word32 -> IO Int32
memoryReadByte s address = do
  segment :: Maybe SegmentRef <- byteSegment s address
  case segment of
    Just (array, offset, _) -> fromIntegral . (fromIntegral :: Word8 -> Int8') <$> readArray array offset
    Nothing -> badMemoryRead s address 0

-- | Load a half word, sign-extended (C: @mem_read_half@).
memoryReadHalf :: Spim -> Word32 -> IO Int32
memoryReadHalf s address = do
  segment :: Maybe SegmentRef <- byteSegment s address
  case segment of
    Just (array, offset, _) | address .&. 1 == 0 -> fromIntegral . (fromIntegral :: Word32 -> Int16') <$> readBytes array offset 2
    _ -> badMemoryRead s address 1

-- | Load a word (C: @mem_read_word@).
memoryReadWord :: Spim -> Word32 -> IO Int32
memoryReadWord s address = do
  segment :: Maybe SegmentRef <- byteSegment s address
  case segment of
    Just (array, offset, _) | address .&. 3 == 0 -> fromIntegral <$> readBytes array offset 4
    _ -> badMemoryRead s address 3

type Int8' = Int8

type Int16' = Int16

memoryWriteByte :: Spim -> Word32 -> Int32 -> IO ()
memoryWriteByte s address v = do
  segment :: Maybe SegmentRef <- byteSegment s address
  case segment of
    Just (array, offset, _) -> writeArray array offset (fromIntegral v)
    Nothing -> badMemoryWrite s address v 0

memoryWriteHalf :: Spim -> Word32 -> Int32 -> IO ()
memoryWriteHalf s address v = do
  segment :: Maybe SegmentRef <- byteSegment s address
  case segment of
    Just (array, offset, _) | address .&. 1 == 0 -> writeBytes array offset 2 (fromIntegral v)
    _ -> badMemoryWrite s address v 1

memoryWriteWord :: Spim -> Word32 -> Int32 -> IO ()
memoryWriteWord s address v = do
  segment :: Maybe SegmentRef <- byteSegment s address
  case segment of
    Just (array, offset, _) | address .&. 3 == 0 -> writeBytes array offset 4 (fromIntegral v)
    _ -> badMemoryWrite s address v 3

-- | Is an address just below the stack, so that touching it should grow
-- the stack?  (More than 16 MB below it is probably a bad pointer.)
belowStack :: Spim -> Word32 -> IO Bool
belowStack s address = do
  dtop :: Word32 <- get (dataTop s)
  sbot :: Word32 <- get (stackBottom s)
  pure (address > dtop && address < sbot && address > sbot - fromIntegral (16 * mega))

-- | The unusual and erroneous loads (C: @bad_mem_read@): misaligned,
-- reading the text segment as data, growing the stack, memory-mapped IO,
-- or no memory at all.
badMemoryRead :: Spim -> Word32 -> Word32 -> IO Int32
badMemoryRead s address mask = do
  ttop :: Word32 <- get (textTop s)
  grow :: Bool <- belowStack s address
  if address .&. mask /= 0
    then raiseAt s address exceptionAddressErrorLoad >> pure 0
    else
      if address >= textBottom && address < ttop
        then do
          slot :: Maybe InstructionRef <- get (textSegment s) >>= \a -> readArray a (fromIntegral ((address - textBottom) `shiftR` 2))
          encoding :: Word32 <- maybe (pure 0) (fmap (fromIntegral . instructionEncoding) . get) slot :: IO Word32
          pure $ case mask of
            0 -> fromIntegral (0xff .&. (encoding `shiftR` (8 * fromIntegral (address .&. 3))))
            1 -> fromIntegral (0xffff .&. (encoding `shiftR` (8 * fromIntegral (address .&. 2))))
            _ -> fromIntegral encoding
        else
          if grow
            then do
              sbot :: Word32 <- get (stackBottom s)
              expandStack s (fromIntegral (sbot - address + 4))
              pure 0
            else
              if address >= memoryMappedIOBottom
                then readMemoryMappedIO s address
                else raiseAt s address exceptionDataBusError >> pure 0

-- | Raise an exception for a bad access to an address, recording the address
-- in BadVAddr.
raiseAt :: Spim -> Word32 -> Int -> IO ()
raiseAt s address code = raiseException s code >> setCoprocessor0 s coprocessor0BadVirtualAddress (fromIntegral address)

-- | The unusual and erroneous stores (C: @bad_mem_write@).  A store into
-- the text segment re-decodes the changed word into an instruction.
badMemoryWrite :: Spim -> Word32 -> Int32 -> Word32 -> IO ()
badMemoryWrite s address value mask = do
  ttop :: Word32 <- get (textTop s)
  grow :: Bool <- belowStack s address
  if address .&. mask /= 0
    then raiseAt s address exceptionAddressErrorStore
    else
      if address >= textBottom && address < ttop
        then do
          array :: IOArray Int (Maybe InstructionRef) <- get (textSegment s)
          let index :: Int
              index = fromIntegral ((address - textBottom) `shiftR` 2)
          slot :: Maybe InstructionRef <- readArray array index
          new :: Int32 <- case slot of
            Nothing -> pure (if mask == 3 then value else 0)
            Just ref -> do
              old :: Word32 <- fromIntegral . instructionEncoding <$> get ref :: IO Word32
              let v :: Word32
                  v = fromIntegral value :: Word32
                  sh :: Word32 -> Int
                  sh n = 8 * fromIntegral (address .&. n)
              pure $ fromIntegral $ case mask of
                0 -> (old .&. complement (0xff `shiftL` sh 3)) .|. ((v .&. 0xff) `shiftL` sh 3)
                1 -> (old .&. complement (0xffff `shiftL` sh 2)) .|. ((v .&. 0xffff) `shiftL` sh 2)
                _ -> v
          ref :: InstructionRef <- newIORef (instructionDecode new)
          writeArray array index (Just ref)
        else
          if grow
            then do
              sbot :: Word32 <- get (stackBottom s)
              expandStack s (fromIntegral (sbot - address + 4))
              sbot' :: Word32 <- get (stackBottom s)
              if address >= sbot'
                then do
                  segment :: IOUArray Int Word8 <- get (stackSegment s)
                  let offset :: Int
                      offset = fromIntegral (address - sbot')
                  case mask of
                    0 -> writeArray segment offset (fromIntegral value)
                    1 -> writeBytes segment offset 2 (fromIntegral value)
                    _ -> writeBytes segment offset 4 (fromIntegral value)
                else raiseAt s address exceptionDataBusError
            else
              if address >= memoryMappedIOBottom
                then writeMemoryMappedIO s address value
                else raiseAt s address exceptionDataBusError

-- The memory-mapped console: a receiver (keyboard) and a transmitter
-- (display), each with a control word and a data word.
receiverControlAddress, receiverBufferAddress, transmitterControlAddress, transmitterBufferAddress :: Word32
receiverControlAddress = 0xffff0000
receiverBufferAddress = 0xffff0004
transmitterControlAddress = 0xffff0008
transmitterBufferAddress = 0xffff000c

receiverReady, receiverInterruptEnable, transmitterReady, transmitterInterruptEnable :: Word32
receiverReady = 1
receiverInterruptEnable = 2
transmitterReady = 1
transmitterInterruptEnable = 2

-- | Set or clear a pending-interrupt bit in the Cause register.
raiseInterrupt, clearInterrupt :: Spim -> Int -> IO ()
raiseInterrupt s level = do
  c :: Int32 <- getCoprocessor0 s coprocessor0Cause
  setCoprocessor0 s coprocessor0Cause (c .|. (1 `shiftL` (level + 8)))
clearInterrupt s level = do
  c :: Int32 <- getCoprocessor0 s coprocessor0Cause
  setCoprocessor0 s coprocessor0Cause (c .&. complement (1 `shiftL` (level + 8)))

-- | Is a byte waiting on standard input?  (Only with -mapped_io.)
consoleInputAvailable :: Spim -> IO Bool
consoleInputAvailable s = do
  mio :: Bool <- get (optionMappedIO s)
  if not mio
    then pure False
    else allocaBytes 8 $ \p -> do
      poke (p :: Ptr CInt) 0
      poke (p `plusPtr` 4 :: Ptr Int16') 1
      poke (p `plusPtr` 6 :: Ptr Int16') 0
      n :: CInt <- c_poll (castP p) 1 0
      pure (n > 0)
  where
    castP :: Ptr CInt -> Ptr ()
    castP p = p `plusPtr` 0

-- | Every IO interval: take a waiting input byte, finish an output byte
-- (C: @check_memory_mapped_IO@).
checkMemoryMappedIO :: Spim -> IO ()
checkMemoryMappedIO s = do
  targetRegister :: Int <- get (receiverTimer s)
  if targetRegister > 0
    then set (receiverTimer s) (targetRegister - 1)
    else do
      avail :: Bool <- consoleInputAvailable s
      when avail $ do
        b :: Maybe Word8 <- readByte
        let c :: Int
            c = maybe 0 fromIntegral b :: Int
        when (c == 3) $ do
          writeError "\nExecution interrupted\n"
          runErrorQuiet
        set (receiverBuffer s) c
        modify (receiverControl s) (.|. receiverReady)
        set (receiverTimer s) 100
        rc :: Word32 <- get (receiverControl s)
        when (rc .&. receiverInterruptEnable /= 0) $ raiseInterrupt s 3
  tt :: Int <- get (transmitterTimer s)
  if tt > 0
    then set (transmitterTimer s) (tt - 1)
    else do
      tc :: Word32 <- get (transmitterControl s)
      when (tc .&. transmitterReady == 0) $ do
        set (transmitterControl s) (tc .|. transmitterReady)
        when (tc .&. transmitterInterruptEnable /= 0) $ raiseInterrupt s 2
  where
    runErrorQuiet :: IO a
    runErrorQuiet = runError ""

writeMemoryMappedIO :: Spim -> Word32 -> Int32 -> IO ()
writeMemoryMappedIO s address value
  | address == transmitterControlAddress =
      if v .&. transmitterInterruptEnable /= 0
        then do
          modify (transmitterControl s) (.|. transmitterInterruptEnable)
          tc :: Word32 <- get (transmitterControl s)
          when (tc .&. transmitterReady /= 0) $ raiseInterrupt s 2
        else do
          modify (transmitterControl s) (.&. complement transmitterInterruptEnable)
          clearInterrupt s 2
  | address == transmitterBufferAddress = do
      tc :: Word32 <- get (transmitterControl s)
      when (tc .&. transmitterReady /= 0) $ do
        let c :: Int
            c = fromIntegral (value .&. 0xff)
        set (transmitterBuffer s) c
        writeOut [chr c]
        set (transmitterControl s) (tc .&. complement transmitterReady)
        set (transmitterTimer s) 100
        clearInterrupt s 2
  | address == receiverControlAddress =
      if v .&. receiverInterruptEnable /= 0
        then do
          modify (receiverControl s) (.|. receiverInterruptEnable)
          rc :: Word32 <- get (receiverControl s)
          when (rc .&. receiverReady /= 0) $ raiseInterrupt s 3
        else do
          modify (receiverControl s) (.&. complement receiverInterruptEnable)
          clearInterrupt s 3
  | address == receiverBufferAddress = pure ()
  | otherwise = runError ("Write to unused memory-mapped IO address (0x" ++ hex address ++ ")\n")
  where
    v :: Word32
    v = fromIntegral value :: Word32

readMemoryMappedIO :: Spim -> Word32 -> IO Int32
readMemoryMappedIO s address
  | address == transmitterControlAddress = fromIntegral <$> get (transmitterControl s)
  | address == transmitterBufferAddress = fromIntegral . (.&. 0xff) <$> get (transmitterBuffer s)
  | address == receiverControlAddress = fromIntegral <$> get (receiverControl s)
  | address == receiverBufferAddress = do
      modify (receiverControl s) (.&. complement receiverReady)
      set (receiverTimer s) 0
      clearInterrupt s 3
      fromIntegral . (.&. 0xff) <$> get (receiverBuffer s)
  | otherwise = runError ("Read from unused memory-mapped IO address (0x" ++ hex address ++ ")\n")

hex :: Word32 -> String
hex n = showHex n ""
