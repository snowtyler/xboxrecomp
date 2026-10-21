"""POP ESP must use the runtime's read/increment/assign ordering."""
from .disasm import BasicBlock, Instruction, Operand
from .lifter import Lifter, lift_basic_block


def test_pop_esp_uses_alias_safe_macro():
    insn = Instruction(0x1000, 1, "pop", "esp", "5c")
    operand = Operand(type="reg", reg="esp")
    operand.size = 4
    insn.operands = [operand]
    statements, _ = lift_basic_block(Lifter(), BasicBlock(start=0x1000, instructions=[insn]))
    assert "POP32(esp, esp);" in statements
