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
  ( Node (..),
    NodeKind (..),
    LabelKind (..),
    printAst,
    printAstJson,
  )
where

import CFormat (formatG)
import Data.Char (ord)
import Data.Int (Int32)
import Data.Word (Word32)
import OpcodeTables (opcodeTokenName)
import Text.Printf (PrintfArg, printf)
import Types (ImmediateExpression (..), Label (..))

-- | One node: where it came from, and what it is.
data Node = Node
  { -- | the source line number
    nodeLine :: Int,
    -- | the source text, as "NNN: text"
    nodeSource :: Maybe String,
    nodeKind :: NodeKind
  }

-- | A label definition: @name:@ places it, @name = value@ makes a constant.
data LabelKind = LabelNormal | LabelConstant Int32

-- | The kinds of node.  Instruction operands are register numbers; an
-- immediate is an 'ImmExpr'.
data NodeKind
  = -- | op rd rs rt
    InstructionR Int Int Int Int
  | -- | op rd rt shamt
    InstructionRShift Int Int Int Int
  | -- | op rt rs imm
    InstructionI Int Int Int ImmediateExpression
  | -- | op target
    InstructionJ Int ImmediateExpression
  | -- | op fd fs ft
    InstructionFloatingPointR Int Int Int Int
  | -- | op fs ft cc
    InstructionFloatingPointCompare Int Int Int Int
  | -- | a pseudo-instruction and what it expanded to
    Pseudo String [Node]
  | DataByte [ImmediateExpression]
  | DataHalf [ImmediateExpression]
  | DataWord [ImmediateExpression]
  | DataFloat [Double]
  | DataDouble [Double]
  | -- | the bytes, and whether to add a NUL
    DataString String Bool
  | DirectiveText (Maybe Word32)
  | DirectiveData (Maybe Word32)
  | DirectiveKtext (Maybe Word32)
  | DirectiveKdata (Maybe Word32)
  | DirectiveAlign Int
  | DirectiveSpace Int
  | DirectiveGlobl String
  | DirectiveExtern String Int
  | DirectiveComm String Int
  | LabelDefinition String LabelKind
  | -- | the root: the file name and its statements
    File (Maybe String) [Node]

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
      File source kids -> "FILE source=" ++ maybe "(stdin)" id source ++ "\n" ++ children kids
      InstructionR op destinationRegister sourceRegister targetRegister -> printf "INST_R op=%s rd=%d rs=%d rt=%d\n" (opcodeTokenName op) destinationRegister sourceRegister targetRegister
      InstructionRShift op destinationRegister targetRegister sh -> printf "INST_R_SHIFT op=%s rd=%d rt=%d shamt=%d\n" (opcodeTokenName op) destinationRegister targetRegister sh
      InstructionI op targetRegister sourceRegister immediate -> printf "INST_I op=%s rt=%d rs=%d imm=" (opcodeTokenName op) targetRegister sourceRegister ++ showImmediate immediate ++ "\n"
      InstructionJ op target -> printf "INST_J op=%s target=" (opcodeTokenName op) ++ showImmediate target ++ "\n"
      InstructionFloatingPointR op floatingPointDestination floatingPointSource floatingPointTarget -> printf "INST_FP_R op=%s fd=%d fs=%d ft=%d\n" (opcodeTokenName op) floatingPointDestination floatingPointSource floatingPointTarget
      InstructionFloatingPointCompare op floatingPointSource floatingPointTarget conditionCode -> printf "INST_FP_COMPARE op=%s fs=%d ft=%d cc=%d\n" (opcodeTokenName op) floatingPointSource floatingPointTarget conditionCode
      Pseudo name kids -> "PSEUDO mnemonic=" ++ name ++ "\n" ++ children kids
      DataByte es -> integerList "DATA_BYTE" es
      DataHalf es -> integerList "DATA_HALF" es
      DataWord es -> integerList "DATA_WORD" es
      DataFloat vs -> floatingPointList "DATA_FLOAT" vs
      DataDouble vs -> floatingPointList "DATA_DOUBLE" vs
      DataString bytes nul ->
        printf "DATA_STRING len=%d null_term=%s value=\"" (length bytes) (if nul then "yes" else "no" :: String)
          ++ concatMap escape bytes
          ++ "\"\n"
      DirectiveText a -> segment "DIR_TEXT" a
      DirectiveData a -> segment "DIR_DATA" a
      DirectiveKtext a -> segment "DIR_KTEXT" a
      DirectiveKdata a -> segment "DIR_KDATA" a
      DirectiveAlign n -> printf "DIR_ALIGN n=%d\n" n
      DirectiveSpace n -> printf "DIR_SPACE size=%d\n" n
      DirectiveGlobl name -> "DIR_GLOBL name=" ++ name ++ "\n"
      DirectiveExtern name size -> printf "DIR_EXTERN name=%s size=%d\n" name size
      DirectiveComm name size -> printf "DIR_COMM name=%s size=%d\n" name size
      LabelDefinition name LabelNormal -> "LABEL_DEF name=" ++ name ++ " (placement)\n"
      LabelDefinition name (LabelConstant v) -> printf "LABEL_DEF name=%s = %d (constant)\n" name v
    integerList :: String -> [ImmediateExpression] -> String
    integerList tag es =
      printf "%s count=%d values=[" (tag :: String) (length es)
        ++ joinWith ", " (map showImmediate es)
        ++ "]\n"
    floatingPointList :: String -> [Double] -> String
    floatingPointList tag vs =
      printf "%s count=%d values=[" (tag :: String) (length vs)
        ++ joinWith ", " (map (formatG 6) vs)
        ++ "]\n"
    segment :: (PrintfArg t) => String -> Maybe t -> String
    segment tag (Just a) = printf "%s start=0x%08x\n" (tag :: String) a
    segment tag Nothing = tag ++ "\n"
    escape :: Char -> String
    escape c
      | c == '\n' = "\\n"
      | c == '\t' = "\\t"
      | c == '\\' || c == '"' = ['\\', c]
      | ord c >= 0x20 && ord c < 0x7f = [c]
      | otherwise = printf "\\x%02x" (ord c)

-- | An immediate as the text dump shows it: @sym+4@, @sym-4@, or a number.
showImmediate :: ImmediateExpression -> String
showImmediate e = case immediateExpressionSymbol e of
  Just l ->
    labelName l
      ++ (if immediateExpressionOffset e > 0 then "+" ++ show (immediateExpressionOffset e) else "")
      ++ (if immediateExpressionOffset e < 0 then show (immediateExpressionOffset e) else "")
      ++ (if immediateExpressionProgramCounterRelative e then " (pc-rel)" else "")
  Nothing -> show (immediateExpressionOffset e)

joinWith :: String -> [String] -> String
joinWith _ [] = ""
joinWith separator (x : xs) = x ++ concatMap (separator ++) xs

-- | The JSON dump, one line (C: @ast_print_json@).
printAstJson :: Node -> String
printAstJson n = jsonNode n ++ "\n"

jsonNode :: Node -> String
jsonNode node = "{\"kind\":\"" ++ kindName ++ "\",\"source_line\":" ++ show (nodeLine node) ++ fields ++ "}"
  where
    kindName :: String
    fields :: String
    (kindName, fields) = case nodeKind node of
      File source kids ->
        ("FILE", maybe "" (\s -> ",\"source_file\":" ++ jsonString s) source ++ ",\"children\":" ++ chain kids)
      InstructionR op destinationRegister sourceRegister targetRegister -> ("INST_R", opcodeField op ++ printf ",\"rd\":%d,\"rs\":%d,\"rt\":%d" destinationRegister sourceRegister targetRegister)
      InstructionRShift op destinationRegister targetRegister sh -> ("INST_R_SHIFT", opcodeField op ++ printf ",\"rd\":%d,\"rt\":%d,\"shamt\":%d" destinationRegister targetRegister sh)
      InstructionI op targetRegister sourceRegister immediate -> ("INST_I", opcodeField op ++ printf ",\"rt\":%d,\"rs\":%d,\"imm\":" targetRegister sourceRegister ++ jsonImmediate immediate)
      InstructionJ op t -> ("INST_J", opcodeField op ++ ",\"target\":" ++ jsonImmediate t)
      InstructionFloatingPointR op floatingPointDestination floatingPointSource floatingPointTarget -> ("INST_FP_R", opcodeField op ++ printf ",\"fd\":%d,\"fs\":%d,\"ft\":%d" floatingPointDestination floatingPointSource floatingPointTarget)
      InstructionFloatingPointCompare op floatingPointSource floatingPointTarget conditionCode ->
        ("INST_FP_COMPARE", opcodeField op ++ printf ",\"fs\":%d,\"ft\":%d,\"cc\":%d" floatingPointSource floatingPointTarget conditionCode)
      Pseudo name kids -> ("PSEUDO", ",\"mnemonic\":" ++ jsonString name ++ ",\"children\":" ++ chain kids)
      DataByte es -> ("DATA_BYTE", integerValues es)
      DataHalf es -> ("DATA_HALF", integerValues es)
      DataWord es -> ("DATA_WORD", integerValues es)
      DataFloat vs -> ("DATA_FLOAT", floatingPointValues vs)
      DataDouble vs -> ("DATA_DOUBLE", floatingPointValues vs)
      DataString bytes nul ->
        ( "DATA_STRING",
          printf ",\"length\":%d,\"null_terminate\":%s,\"value\":" (length bytes) (jsonBool nul) ++ jsonString bytes
        )
      DirectiveText a -> ("DIR_TEXT", start a)
      DirectiveData a -> ("DIR_DATA", start a)
      DirectiveKtext a -> ("DIR_KTEXT", start a)
      DirectiveKdata a -> ("DIR_KDATA", start a)
      DirectiveAlign n -> ("DIR_ALIGN", printf ",\"n\":%d" n)
      DirectiveSpace n -> ("DIR_SPACE", printf ",\"size\":%d" n)
      DirectiveGlobl name -> ("DIR_GLOBL", ",\"name\":" ++ jsonString name)
      DirectiveExtern name size -> ("DIR_EXTERN", ",\"name\":" ++ jsonString name ++ printf ",\"size\":%d" size)
      DirectiveComm name size -> ("DIR_COMM", ",\"name\":" ++ jsonString name ++ printf ",\"size\":%d" size)
      LabelDefinition name LabelNormal -> ("LABEL_DEF", ",\"name\":" ++ jsonString name ++ ",\"label_kind\":\"placement\"")
      LabelDefinition name (LabelConstant v) ->
        ("LABEL_DEF", ",\"name\":" ++ jsonString name ++ ",\"label_kind\":\"constant\"" ++ printf ",\"value\":%d" v)
    opcodeField :: Int -> String
    opcodeField op = printf ",\"op\":%d,\"mnemonic\":" op ++ jsonString (opcodeTokenName op)
    chain :: [Node] -> String
    chain kids = "[" ++ joinWith "," (map jsonNode kids) ++ "]"
    integerValues :: [ImmediateExpression] -> String
    integerValues es = printf ",\"count\":%d,\"values\":" (length es) ++ "[" ++ joinWith "," (map jsonImmediate es) ++ "]"
    floatingPointValues :: [Double] -> String
    floatingPointValues vs = printf ",\"count\":%d,\"values\":[" (length vs) ++ joinWith "," (map (formatG 17) vs) ++ "]"
    start :: (PrintfArg t) => Maybe t -> String
    start (Just a) = printf ",\"start_addr\":%u" a
    start Nothing = ""
    jsonBool :: Bool -> String
    jsonBool b = if b then "true" else "false" :: String

jsonImmediate :: ImmediateExpression -> String
jsonImmediate e =
  "{\"offset\":"
    ++ show (immediateExpressionOffset e)
    ++ ",\"symbol\":"
    ++ maybe "null" (jsonString . labelName) (immediateExpressionSymbol e)
    ++ ",\"pc_relative\":"
    ++ (if immediateExpressionProgramCounterRelative e then "true" else "false")
    ++ "}"

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
