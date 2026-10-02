# @category SFSE
# # @runtime PyGhidra
#
# TypeOfSingleton.py
#
# Dado un conjunto de direcciones de singleton ya conocidas, imprime el
# nombre RTTI de cada una caminando la cadena MSVC hacia atrás:
#
#   singleton → [singleton+0] = vtable
#             → [vtable-8]    = CompleteObjectLocator
#             → [COL+0x0C]    = pTypeDescriptor (RVA)
#             → [TD+0x10]     = name string
#
# No depende del análisis RTTI de Ghidra: lee memoria cruda.

program   = currentProgram
memory    = program.getMemory()
imageBase = program.getImageBase().getOffset()

# RVAs de los cuatro singletons consecutivos en la región 0x59783xx.
# Orden por dirección, no por evento (aún no sabemos qué es qué).
SINGLETONS = [
    ("Slot 1", 0x59783C8),
    ("Slot 2", 0x59783F0),
    ("Slot 3", 0x5978418),
    ("Slot 4", 0x5978440),
]

def to_addr(offset):
    return program.getAddressFactory().getDefaultAddressSpace().getAddress(offset)

def u64(addr):
    return memory.getLong(addr) & 0xFFFFFFFFFFFFFFFF

def u32(addr):
    return memory.getInt(addr) & 0xFFFFFFFF

def read_cstring(addr, maxlen=512):
    out = []
    for i in range(maxlen):
        b = memory.getByte(addr.add(i)) & 0xFF
        if b == 0:
            break
        out.append(chr(b))
    return "".join(out)

print("")
print("=== TypeOfSingleton.py ===")
print("ImageBase: 0x%X" % imageBase)
print("")

for label, rva in SINGLETONS:
    print("--------------------------------------------------")
    print("%s @ RVA 0x%X" % (label, rva))

    singleton = to_addr(imageBase + rva)

    # 1. vtable = [singleton + 0]
    try:
        vtbl = u64(singleton)
    except Exception as e:
        print("  ERROR leyendo singleton: %s" % e)
        continue

    if vtbl < imageBase or vtbl > imageBase + 0x100000000:
        print("  vtable 0x%X fuera del ejecutable" % vtbl)
        continue

    print("  vtable RVA: 0x%X" % (vtbl - imageBase))

    # 2. COL = [vtable - 8]
    try:
        col = u64(to_addr(vtbl - 8))
    except Exception as e:
        print("  ERROR leyendo COL: %s" % e)
        continue

    if col < imageBase or col > imageBase + 0x100000000:
        print("  COL 0x%X fuera del ejecutable" % col)
        continue

    print("  COL RVA:    0x%X" % (col - imageBase))

    # 3. pTypeDescriptor: RVA (uint32) en COL + 0x0C
    try:
        td_rva = u32(to_addr(col + 0x0C))
    except Exception as e:
        print("  ERROR leyendo pTypeDescriptor: %s" % e)
        continue

    td_addr = to_addr(imageBase + td_rva)
    print("  TD RVA:     0x%X" % td_rva)

    # 4. name string en TD + 0x10
    try:
        name = read_cstring(td_addr.add(0x10))
    except Exception as e:
        print("  ERROR leyendo name: %s" % e)
        continue

    print("  TypeName: %s" % name)
    print("")

print("=== Fin ===")

