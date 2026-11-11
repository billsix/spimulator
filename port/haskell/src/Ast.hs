-- | The assembler's abstract syntax tree, and its two printers
-- (C: include/ast.h, src/ast.c).
--
-- The parser turns a file into a list of these nodes; the emitter then
-- walks the list and commits each node to memory.  @-print-ast@ prints the
-- tree as indented text, @-print-ast-json@ as JSON, and @-show-expansion@
-- prints just the pseudo-instructions and what they expand to.
--
-- The tree is plain immutable data and both printers are pure functions
-- from a tree to text.
module Ast
  ( Node (..)
  , NodeKind (..)
  , LabelKind (..)
  , printAst
  , printAstJson
  ) where

import CFormat (formatG)
import Data.Char (ord)
import Data.Int (Int32)
import Data.Word (Word32)
import OpcodeTables (opTokenName)
import Text.Printf (printf, PrintfArg)
import Types (ImmExpr (..), Label (..))

-- | One node: where it came from, and what it is.
data Node = Node
  { nodeLine :: Int -- ^ the source line number
  , nodeSrc :: Maybe String -- ^ the source text, as "NNN: text"
  , nodeKind :: NodeKind
  }

-- | A label definition: @name:@ places it, @name = value@ makes a constant.
data LabelKind = LabelNormal | LabelConst Int32

-- | The kinds of node.  Instruction operands are register numbers; an
-- immediate is an 'ImmExpr'.
data NodeKind
  = InstR Int Int Int Int -- ^ op rd rs rt
  | InstRShift Int Int Int Int -- ^ op rd rt shamt
  | InstI Int Int Int ImmExpr -- ^ op rt rs imm
  | InstJ Int ImmExpr -- ^ op target
  | InstFpR Int Int Int Int -- ^ op fd fs ft
  | InstFpCompare Int Int Int Int -- ^ op fs ft cc
  | Pseudo String [Node] -- ^ a pseudo-instruction and what it expanded to
  | DataByte [ImmExpr]
  | DataHalf [ImmExpr]
  | DataWord [ImmExpr]
  | DataFloat [Double]
  | DataDouble [Double]
  | DataString String Bool -- ^ the bytes, and whether to add a NUL
  | DirText (Maybe Word32)
  | DirData (Maybe Word32)
  | DirKText (Maybe Word32)
  | DirKData (Maybe Word32)
  | DirAlign Int
  | DirSpace Int
  | DirGlobl String
  | DirExtern String Int
  | DirComm String Int
  | LabelDef String LabelKind
  | File (Maybe String) [Node] -- ^ the root: the file name and its statements

-- | The indented text dump (C: @ast_print@).
printAst :: Node -> String
printAst = printNode 0

printNode :: Int -> Node -> String
printNode depth node =
  concat (replicate depth "  ") ++ printf "[line %d] " (nodeLine node) ++ body
  where
    children :: [Node] -> String
    children = concatMap (printNode (depth + 1))
    body :: String
    body = case nodeKind node of
      File src kids -> "FILE source=" ++ maybe "(stdin)" id src ++ "\n" ++ children kids
      InstR op rd rs rt -> printf "INST_R op=%d rd=%d rs=%d rt=%d\n" op rd rs rt
      InstRShift op rd rt sh -> printf "INST_R_SHIFT op=%d rd=%d rt=%d shamt=%d\n" op rd rt sh
      InstI op rt rs imm -> printf "INST_I op=%d rt=%d rs=%d imm=" op rt rs ++ showImm imm ++ "\n"
      InstJ op target -> printf "INST_J op=%d target=" op ++ showImm target ++ "\n"
      InstFpR op fd fs ft -> printf "INST_FP_R op=%d fd=%d fs=%d ft=%d\n" op fd fs ft
      InstFpCompare op fs ft cc -> printf "INST_FP_COMPARE op=%d fs=%d ft=%d cc=%d\n" op fs ft cc
      Pseudo name kids -> "PSEUDO mnemonic=" ++ name ++ "\n" ++ children kids
      DataByte es -> intList "DATA_BYTE" es
      DataHalf es -> intList "DATA_HALF" es
      DataWord es -> intList "DATA_WORD" es
      DataFloat vs -> fpList "DATA_FLOAT" vs
      DataDouble vs -> fpList "DATA_DOUBLE" vs
      DataString bytes nul ->
        printf "DATA_STRING len=%d null_term=%s value=\"" (length bytes) (if nul then "yes" else "no" :: String)
          ++ concatMap escape bytes ++ "\"\n"
      DirText a -> seg "DIR_TEXT" a
      DirData a -> seg "DIR_DATA" a
      DirKText a -> seg "DIR_KTEXT" a
      DirKData a -> seg "DIR_KDATA" a
      DirAlign n -> printf "DIR_ALIGN n=%d\n" n
      DirSpace n -> printf "DIR_SPACE size=%d\n" n
      DirGlobl name -> "DIR_GLOBL name=" ++ name ++ "\n"
      DirExtern name size -> printf "DIR_EXTERN name=%s size=%d\n" name size
      DirComm name size -> printf "DIR_COMM name=%s size=%d\n" name size
      LabelDef name LabelNormal -> "LABEL_DEF name=" ++ name ++ " (placement)\n"
      LabelDef name (LabelConst v) -> printf "LABEL_DEF name=%s = %d (constant)\n" name v
    intList :: String -> [ImmExpr] -> String
    intList tag es =
      printf "%s count=%d values=[" (tag :: String) (length es)
        ++ joinWith ", " (map showImm es) ++ "]\n"
    fpList :: String -> [Double] -> String
    fpList tag vs =
      printf "%s count=%d values=[" (tag :: String) (length vs)
        ++ joinWith ", " (map (formatG 6) vs) ++ "]\n"
    seg :: PrintfArg t => String -> Maybe t -> String
    seg tag (Just a) = printf "%s start=0x%08x\n" (tag :: String) a
    seg tag Nothing = tag ++ "\n"
    escape :: Char -> String
    escape c
      | c == '\n' = "\\n"
      | c == '\t' = "\\t"
      | c == '\\' || c == '"' = ['\\', c]
      | ord c >= 0x20 && ord c < 0x7f = [c]
      | otherwise = printf "\\x%02x" (ord c)

-- | An immediate as the text dump shows it: @sym+4@, @sym-4@, or a number.
showImm :: ImmExpr -> String
showImm e = case ieSymbol e of
  Just l ->
    lblName l
      ++ (if ieOffset e > 0 then "+" ++ show (ieOffset e) else "")
      ++ (if ieOffset e < 0 then show (ieOffset e) else "")
      ++ (if iePcRelative e then " (pc-rel)" else "")
  Nothing -> show (ieOffset e)

joinWith :: String -> [String] -> String
joinWith _ [] = ""
joinWith sep (x : xs) = x ++ concatMap (sep ++) xs

-- | The JSON dump, one line (C: @ast_print_json@).
printAstJson :: Node -> String
printAstJson n = jsonNode n ++ "\n"

jsonNode :: Node -> String
jsonNode node = "{\"kind\":\"" ++ kindName ++ "\",\"source_line\":" ++ show (nodeLine node) ++ fields ++ "}"
  where
    kindName :: String
    fields :: String
    (kindName, fields) = case nodeKind node of
      File src kids ->
        ("FILE", maybe "" (\s -> ",\"source_file\":" ++ jsonString s) src ++ ",\"children\":" ++ chain kids)
      InstR op rd rs rt -> ("INST_R", opField op ++ printf ",\"rd\":%d,\"rs\":%d,\"rt\":%d" rd rs rt)
      InstRShift op rd rt sh -> ("INST_R_SHIFT", opField op ++ printf ",\"rd\":%d,\"rt\":%d,\"shamt\":%d" rd rt sh)
      InstI op rt rs imm -> ("INST_I", opField op ++ printf ",\"rt\":%d,\"rs\":%d,\"imm\":" rt rs ++ jsonImm imm)
      InstJ op t -> ("INST_J", opField op ++ ",\"target\":" ++ jsonImm t)
      InstFpR op fd fs ft -> ("INST_FP_R", opField op ++ printf ",\"fd\":%d,\"fs\":%d,\"ft\":%d" fd fs ft)
      InstFpCompare op fs ft cc ->
        ("INST_FP_COMPARE", opField op ++ printf ",\"fs\":%d,\"ft\":%d,\"cc\":%d" fs ft cc)
      Pseudo name kids -> ("PSEUDO", ",\"mnemonic\":" ++ jsonString name ++ ",\"children\":" ++ chain kids)
      DataByte es -> ("DATA_BYTE", intValues es)
      DataHalf es -> ("DATA_HALF", intValues es)
      DataWord es -> ("DATA_WORD", intValues es)
      DataFloat vs -> ("DATA_FLOAT", fpValues vs)
      DataDouble vs -> ("DATA_DOUBLE", fpValues vs)
      DataString bytes nul ->
        ( "DATA_STRING"
        , printf ",\"length\":%d,\"null_terminate\":%s,\"value\":" (length bytes) (jsonBool nul) ++ jsonString bytes
        )
      DirText a -> ("DIR_TEXT", start a)
      DirData a -> ("DIR_DATA", start a)
      DirKText a -> ("DIR_KTEXT", start a)
      DirKData a -> ("DIR_KDATA", start a)
      DirAlign n -> ("DIR_ALIGN", printf ",\"n\":%d" n)
      DirSpace n -> ("DIR_SPACE", printf ",\"size\":%d" n)
      DirGlobl name -> ("DIR_GLOBL", ",\"name\":" ++ jsonString name)
      DirExtern name size -> ("DIR_EXTERN", ",\"name\":" ++ jsonString name ++ printf ",\"size\":%d" size)
      DirComm name size -> ("DIR_COMM", ",\"name\":" ++ jsonString name ++ printf ",\"size\":%d" size)
      LabelDef name LabelNormal -> ("LABEL_DEF", ",\"name\":" ++ jsonString name ++ ",\"label_kind\":\"placement\"")
      LabelDef name (LabelConst v) ->
        ("LABEL_DEF", ",\"name\":" ++ jsonString name ++ ",\"label_kind\":\"constant\"" ++ printf ",\"value\":%d" v)
    opField :: Int -> String
    opField op = printf ",\"op\":%d,\"mnemonic\":" op ++ jsonString (opTokenName op)
    chain :: [Node] -> String
    chain kids = "[" ++ joinWith "," (map jsonNode kids) ++ "]"
    intValues :: [ImmExpr] -> String
    intValues es = printf ",\"count\":%d,\"values\":" (length es) ++ "[" ++ joinWith "," (map jsonImm es) ++ "]"
    fpValues :: [Double] -> String
    fpValues vs = printf ",\"count\":%d,\"values\":[" (length vs) ++ joinWith "," (map (formatG 17) vs) ++ "]"
    start :: PrintfArg t => Maybe t -> String
    start (Just a) = printf ",\"start_addr\":%u" a
    start Nothing = ""
    jsonBool :: Bool -> String
    jsonBool b = if b then "true" else "false" :: String

jsonImm :: ImmExpr -> String
jsonImm e =
  "{\"offset\":" ++ show (ieOffset e) ++ ",\"symbol\":"
    ++ maybe "null" (jsonString . lblName) (ieSymbol e)
    ++ ",\"pc_relative\":" ++ (if iePcRelative e then "true" else "false") ++ "}"

-- | A JSON string literal; control bytes become @\\uXXXX@.
jsonString :: String -> String
jsonString s = "\"" ++ concatMap esc s ++ "\""
  where
    esc :: Char -> String
    esc c = case c of
      '"' -> "\\\""
      '\\' -> "\\\\"
      '\n' -> "\\n"
      '\r' -> "\\r"
      '\t' -> "\\t"
      '\b' -> "\\b"
      '\f' -> "\\f"
      _
        | ord c < 0x20 -> printf "\\u%04x" (ord c)
        | otherwise -> [c]
