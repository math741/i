"""Operações da MLG. Cada uma é só dados para o núcleo: SPEC, TRANSFORMATION SPACE,
EVIDENCE OBLIGATIONS e COST MODEL. O núcleo (engine.py) não conhece nenhuma delas."""
from .attention import Attention
from .gemm import Gemm

OPS = {op.name: op for op in (Gemm(), Attention())}
