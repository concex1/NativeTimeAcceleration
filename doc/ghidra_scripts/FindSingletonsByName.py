# -*- coding: utf-8 -*-
# ============================================================================
# FindSingletonsByName.py
# ----------------------------------------------------------------------------
# Localiza instancias singleton y estructuras RTTI asociadas por substring
# del nombre de clase desmangado. Pensado para el binario de Starfield.exe
# cargado en Ghidra 11.x con PyGhidra.
#
# Metodología (MSVC x64 RTTI):
#   1. Busca _TypeDescriptor en .rdata cuyo nombre (".?AV..." / ".?AU...")
#      contenga alguno de los substrings dados.
#   2. Para cada TD, localiza la _RTTICompleteObjectLocator (COL) que lo
#      referencia desde su campo pTypeDescriptor (RVA de 4 bytes).
#   3. Para cada COL, localiza la vtable cuya entrada [-1] apunta a la COL
#      (dirección absoluta de 8 bytes en x64).
#   4. Para cada vtable, busca referencias de datos (xrefs) que apunten a
#      ella. Cada xref de datos es un objeto cuyo primer miembro es el
#      puntero a la vtable: candidato directo a instancia singleton.
#
# Uso:
#   Ejecutar el script sin argumentos -> abre un diálogo pidiendo substrings.
#   Ejecutar con argumentos (barra espaciadora -> "Run with arguments") y
#   pasar una cadena con substrings separados por comas:
#       TimeMultiplierManager,Calendar,PlayerCharacter
#
# Salida:
#   Por cada TD que coincida:
#     - Nombre RTTI completo y dirección del TD
#     - COL asociada
#     - Vtable(s) asociada(s)
#     - Referencias de datos (candidatos a singleton) con volcado de los
#       primeros 8 qwords de cada objeto candidato.
# ============================================================================

from ghidra.program.model.symbol import RefType

# ----------------------------------------------------------------------------
# Utilidades
# ----------------------------------------------------------------------------

def _hex(addr_or_long):
    if hasattr(addr_or_long, 'getOffset'):
        return "0x%X" % (addr_or_long.getOffset() & 0xFFFFFFFFFFFFFFFF)
    return "0x%X" % (int(addr_or_long) & 0xFFFFFFFFFFFFFFFF)


def _read_dword(addr):
    return currentProgram.getMemory().getInt(addr) & 0xFFFFFFFF


def _read_qword(addr):
    return currentProgram.getMemory().getLong(addr) & 0xFFFFFFFFFFFFFFFF


def _read_string(addr, max_len=256):
    mem = currentProgram.getMemory()
    out = []
    for i in range(max_len):
        try:
            b = mem.getByte(addr.add(i)) & 0xFF
        except Exception:
            break
        if b == 0:
            break
        out.append(b)
    try:
        return bytes(out).decode('ascii')
    except Exception:
        return None


def _refs_to(addr):
    try:
        return list(currentProgram.getReferenceManager().getReferencesTo(addr))
    except Exception:
        return []


def _is_data_ref(ref):
    """Heurística: nos interesan referencias de datos, no saltos ni calls."""
    try:
        rt = str(ref.getReferenceType()).upper()
    except Exception:
        return False
    if 'COMPUTED' in rt or 'INDIRECT' in rt:
        return False
    if 'DATA' in rt or 'READ' in rt or 'WRITE' in rt or 'PARAM' in rt or 'UNCONDITIONAL' not in rt:
        return True
    return False


# ----------------------------------------------------------------------------
# Búsqueda de TypeDescriptors
# ----------------------------------------------------------------------------

def find_type_descriptors(substrings):
    """
    Recorre todas las cadenas definidas que empiezan por .?AV o .?AU y cuyo
    nombre contiene alguno de los substrings. Devuelve una lista de tuplas
    (td_addr, name, matched_substrings).
    """
    results = []
    listing = currentProgram.getListing()
    data_iter = listing.getDefinedData(True)
    subs_lower = [s.lower() for s in substrings]

    while data_iter.hasNext():
        d = data_iter.next()
        try:
            if not d.hasStringValue():
                continue
        except Exception:
            continue
        try:
            s = d.getValue()
        except Exception:
            continue
        if not isinstance(s, str):
            continue
        if not (s.startswith(".?AV") or s.startswith(".?AU")):
            continue

        s_lower = s.lower()
        matched = [sub for sub in subs_lower if sub in s_lower]
        if not matched:
            continue

        str_addr = d.getAddress()
        # Layout del _TypeDescriptor en x64:
        #   +0x00 vtable ptr (8 bytes)
        #   +0x08 spare      (8 bytes)
        #   +0x10 nombre ASCII nul-terminado
        td_addr = str_addr.subtract(0x10)

        results.append((td_addr, s, matched))

    return results


# ----------------------------------------------------------------------------
# Búsqueda de COL (Complete Object Locator)
# ----------------------------------------------------------------------------

def find_col_for_td(td_addr):
    """
    Busca la COL cuya pTypeDescriptor apunte a td_addr. En x64, el campo es
    un RVA (dword). Devuelve la lista de direcciones de COL encontradas.

    Estrategia: primero intenta resolver por referencias ya creadas por
    Ghidra; si no hay, recorre .rdata/.data buscando el RVA en cualquier
    posición alineada a 4 bytes.
    """
    image_base = currentProgram.getImageBase().getOffset()
    td_rva = (td_addr.getOffset() - image_base) & 0xFFFFFFFF

    cols = []

    # Vía 1: referencias ya creadas por Ghidra.
    for ref in _refs_to(td_addr):
        try:
            from_addr = ref.getFromAddress()
            # Si la referencia apunta directamente al campo pTypeDescriptor
            # de una COL, then la COL empieza en from_addr - 0x0C.
            col_addr = from_addr.subtract(0x0C)
            try:
                if _read_dword(col_addr) == 1:  # signature x64
                    cols.append(col_addr)
                    continue
            except Exception:
                pass
            # A veces Ghidra crea la referencia al inicio de la COL.
            try:
                if _read_dword(from_addr) == 1:
                    ptd = _read_dword(from_addr.add(0x0C))
                    if ptd == td_rva:
                        cols.append(from_addr)
            except Exception:
                pass
        except Exception:
            continue

    if cols:
        return cols

    # Vía 2: escaneo de .rdata y .data.
    memory = currentProgram.getMemory()
    for block in memory.getBlocks():
        if not block.isInitialized():
            continue
        if block.getName() not in ('.rdata', '.data'):
            continue

        start = block.getStart()
        end = block.getEnd()
        cur = start
        while cur.compareTo(end) < 0:
            try:
                v = _read_dword(cur)
            except Exception:
                break
            if v == td_rva:
                col_addr = cur.subtract(0x0C)
                try:
                    if _read_dword(col_addr) == 1:
                        cols.append(col_addr)
                except Exception:
                    pass
            cur = cur.add(4)

    return cols


# ----------------------------------------------------------------------------
# Búsqueda de vtable
# ----------------------------------------------------------------------------

def find_vtable_for_col(col_addr):
    """
    Busca la vtable cuya entrada [-1] contiene la dirección absoluta de
    col_addr. Devuelve la lista de direcciones de vtable (inicio del array
    de punteros a funciones, i.e. justo después del qword que apunta a COL).
    """
    col_full = col_addr.getOffset() & 0xFFFFFFFFFFFFFFFF
    vtables = []

    memory = currentProgram.getMemory()
    for block in memory.getBlocks():
        if not block.isInitialized():
            continue
        if block.getName() != '.rdata':
            continue

        start = block.getStart()
        end = block.getEnd()
        cur = start
        while cur.compareTo(end) < 0:
            try:
                v = _read_qword(cur)
            except Exception:
                break
            if v == col_full:
                vtables.append(cur.add(8))
            cur = cur.add(8)

    return vtables


# ----------------------------------------------------------------------------
# Búsqueda de instancias singleton (referencias a la vtable)
# ----------------------------------------------------------------------------

def find_singleton_refs(vtable_addr):
    """
    Devuelve todas las referencias de datos a vtable_addr. Cada referencia es
    la dirección de un objeto cuyo primer qword es el puntero a la vtable,
    es decir, un candidato directo a instancia.
    """
    results = []
    for ref in _refs_to(vtable_addr):
        if _is_data_ref(ref):
            results.append((ref.getFromAddress(), str(ref.getReferenceType())))
    return results


def dump_object(addr, n_qwords=8):
    lines = []
    for i in range(n_qwords):
        try:
            q = _read_qword(addr.add(i * 8))
        except Exception:
            break
        lines.append("        +%02X: 0x%016X" % (i * 8, q))
    return lines


# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------

def main():
    args = getScriptArgs()
    if args and len(args) > 0:
        raw = args[0]
    else:
        raw = askString("FindSingletonsByName",
                        "Substrings separados por comas (ej: TimeMultiplierManager)")

    if not raw:
        println("[!] Sin substrings. Abortando.")
        return

    substrings = [s.strip() for s in raw.split(",") if s.strip()]
    if not substrings:
        println("[!] Lista de substrings vacía. Abortando.")
        return

    println("=" * 72)
    println("FindSingletonsByName.py")
    println("Programa:   %s" % currentProgram.getName())
    println("ImageBase:  %s" % _hex(currentProgram.getImageBase()))
    println("Substrings: %s" % ", ".join(substrings))
    println("=" * 72)

    tds = find_type_descriptors(substrings)
    println("[+] TypeDescriptors encontrados: %d" % len(tds))

    for (td_addr, name, matched) in tds:
        println("")
        println("-" * 72)
        println("TYPEDESCRIPTOR %s" % _hex(td_addr))
        println("  Nombre:  %s" % name)
        println("  Match:   %s" % ", ".join(matched))

        cols = find_col_for_td(td_addr)
        println("  COLs:    %d" % len(cols))

        for col_addr in cols:
            println("    COL %s" % _hex(col_addr))

            vtables = find_vtable_for_col(col_addr)
            println("      Vtables: %d" % len(vtables))

            for vt_addr in vtables:
                println("        Vtable %s" % _hex(vt_addr))

                singletons = find_singleton_refs(vt_addr)
                if not singletons:
                    println("          (sin referencias de datos - probablemente")
                    println("           la instancia se accede vía puntero estatico)")
                    continue

                println("          Candidatos a singleton: %d" % len(singletons))
                for (ref_addr, ref_type) in singletons:
                    println("            -> %s  [%s]" % (_hex(ref_addr), ref_type))
                    for line in dump_object(ref_addr, 8):
                        println("          %s" % line)

    println("")
    println("=" * 72)
    println("Fin del barrido. Copia la salida completa para su analisis.")
    println("=" * 72)


main()