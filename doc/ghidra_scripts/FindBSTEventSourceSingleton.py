# -*- coding: utf-8 -*-
# ============================================================================
# FindBSTEventSourceSingleton.py
# ----------------------------------------------------------------------------
# Localiza la instancia singleton de BSTEventSource<T> para un tipo T dado,
# caminando RTTI y clasificando los candidatos encontrados. Pensado para
# diagnosticar casos donde un ID del CSV/IDs_RTTI.h no apunta a un
# BSTEventSource válido (RegisterSink cuelga) pero debería.
#
# Metodología (MSVC x64 RTTI):
#   1. Busca _TypeDescriptor en .rdata cuyo nombre (".?AV..." / ".?AU...")
#      contenga alguno de los substrings dados.
#   2. Para cada TD, localiza la CompleteObjectLocator (COL) que lo
#      referencia vía su campo pTypeDescriptor (RVA de 4 bytes).
#   3. Para cada COL, localiza la vtable cuya entrada [-1] apunta a la COL
#      (dirección absoluta de 8 bytes en x64).
#   4. Para cada vtable, busca referencias de datos (xrefs) hacia ella.
#      Cada xref es un objeto cuyo primer qword es la vtable: candidato
#      directo a instancia singleton.
#   5. Clasifica cada candidato como probable BSTEventSource, objeto con
#      vtable pero contadores inusuales, o no-BSTEventSource.
#
# Uso:
#   Run with arguments:
#     TESCellFullyLoadedEvent
#     TESCellFullyLoadedEvent,BSTEventSource,TimeMultiplierManager
#
# Salida: bloque de diagnóstico por cada TD encontrado. Copia la salida
# completa del log de Ghidra.
# ============================================================================

def _hex(x):
    if hasattr(x, 'getOffset'):
        return "0x%X" % (x.getOffset() & 0xFFFFFFFFFFFFFFFF)
    return "0x%X" % (int(x) & 0xFFFFFFFFFFFFFFFF)


def _read_dword(addr):
    return currentProgram.getMemory().getInt(addr) & 0xFFFFFFFF


def _read_qword(addr):
    return currentProgram.getMemory().getLong(addr) & 0xFFFFFFFFFFFFFFFF


def _refs_to(addr):
    try:
        return list(currentProgram.getReferenceManager().getReferencesTo(addr))
    except Exception:
        return []


def _is_data_ref(ref):
    try:
        rt = str(ref.getReferenceType()).upper()
    except Exception:
        return False
    if 'COMPUTED' in rt or 'INDIRECT' in rt or 'JUMP' in rt or 'CALL' in rt:
        return False
    return 'DATA' in rt or 'READ' in rt or 'WRITE' in rt or 'PARAM' in rt


def find_type_descriptors(substrings):
    subs_lower = [s.lower() for s in substrings]
    results = []
    data_iter = currentProgram.getListing().getDefinedData(True)
    while data_iter.hasNext():
        d = data_iter.next()
        try:
            if not d.hasStringValue():
                continue
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
        # _TypeDescriptor: +0x00 vtable, +0x08 spare, +0x10 name
        td_addr = d.getAddress().subtract(0x10)
        results.append((td_addr, s, matched))
    return results


def find_cols_for_td(td_addr):
    image_base = currentProgram.getImageBase().getOffset()
    td_rva = (td_addr.getOffset() - image_base) & 0xFFFFFFFF
    cols = []

    # Vía 1: xrefs ya creadas por Ghidra
    for ref in _refs_to(td_addr):
        try:
            from_addr = ref.getFromAddress()
            # COL.pTypeDescriptor está en offset +0x0C
            col_addr = from_addr.subtract(0x0C)
            try:
                if _read_dword(col_addr) == 1:
                    if col_addr not in cols:
                        cols.append(col_addr)
                    continue
            except Exception:
                pass
            try:
                if _read_dword(from_addr) == 1:
                    ptd = _read_dword(from_addr.add(0x0C))
                    if ptd == td_rva and from_addr not in cols:
                        cols.append(from_addr)
            except Exception:
                pass
        except Exception:
            continue
    if cols:
        return cols

    # Vía 2: escaneo directo en .rdata / .data
    mem = currentProgram.getMemory()
    for block in mem.getBlocks():
        if not block.isInitialized():
            continue
        if block.getName() not in ('.rdata', '.data'):
            continue
        cur = block.getStart()
        end = block.getEnd()
        while cur.compareTo(end) < 0:
            try:
                if _read_dword(cur) == td_rva:
                    col_addr = cur.subtract(0x0C)
                    try:
                        if _read_dword(col_addr) == 1 and col_addr not in cols:
                            cols.append(col_addr)
                    except Exception:
                        pass
            except Exception:
                break
            cur = cur.add(4)
    return cols


def find_vtables_for_col(col_addr):
    col_full = col_addr.getOffset() & 0xFFFFFFFFFFFFFFFF
    vtables = []
    mem = currentProgram.getMemory()
    for block in mem.getBlocks():
        if not block.isInitialized() or block.getName() != '.rdata':
            continue
        cur = block.getStart()
        end = block.getEnd()
        while cur.compareTo(end) < 0:
            try:
                if _read_qword(cur) == col_full:
                    vtables.append(cur.add(8))
            except Exception:
                break
            cur = cur.add(8)
    return vtables


def classify_candidate(addr):
    """Heurística de clasificación. Devuelve (kind, info)."""
    try:
        vtbl = _read_qword(addr)
        count_cap = _read_qword(addr.add(0x08))
        buf = _read_qword(addr.add(0x10))
    except Exception:
        return ("unreadable", {})

    image_base = currentProgram.getImageBase().getOffset()
    vtbl_in_binary = image_base <= vtbl < image_base + 0x100000000
    count = count_cap & 0xFFFFFFFF
    cap = (count_cap >> 32) & 0xFFFFFFFF

    info = {'vtbl': vtbl, 'vtbl_in_binary': vtbl_in_binary,
            'count': count, 'cap': cap, 'buffer': buf}

    if vtbl_in_binary and 0 <= count <= 0x1000 and 0 <= cap <= 0x1000 and count <= cap:
        return ("probable_bsteventsource", info)
    if vtbl_in_binary:
        return ("has_vtable_weird_counts", info)
    return ("not_bsteventsource", info)


def dump_object(addr, n_qwords=8):
    for i in range(n_qwords):
        try:
            q = _read_qword(addr.add(i * 8))
            println("               +%02X: 0x%016X" % (i * 8, q))
        except Exception:
            break


def main():
    args = getScriptArgs()
    raw = args[0] if args else "TESCellFullyLoadedEvent"
    subs = [s.strip() for s in raw.split(",") if s.strip()]
    if not subs:
        println("[!] Sin substrings.")
        return

    println("=" * 78)
    println("FindBSTEventSourceSingleton.py")
    println("Programa:   %s" % currentProgram.getName())
    println("ImageBase:  %s" % _hex(currentProgram.getImageBase()))
    println("Substrings: %s" % ", ".join(subs))
    println("=" * 78)

    tds = find_type_descriptors(subs)
    println("[+] TypeDescriptors encontrados: %d" % len(tds))

    for td_addr, name, matched in tds:
        println("")
        println("-" * 78)
        println("TYPEDESCRIPTOR %s" % _hex(td_addr))
        println("  Nombre: %s" % name)
        println("  Match:  %s" % ", ".join(matched))

        cols = find_cols_for_td(td_addr)
        println("  COLs:   %d" % len(cols))
        for col_addr in cols:
            println("    COL %s" % _hex(col_addr))
            vtables = find_vtables_for_col(col_addr)
            println("      Vtables: %d" % len(vtables))
            for vt_addr in vtables:
                println("        Vtable %s" % _hex(vt_addr))

                singletons = []
                for ref in _refs_to(vt_addr):
                    if _is_data_ref(ref):
                        singletons.append(ref.getFromAddress())

                if not singletons:
                    println("          (sin referencias de datos)")
                    continue

                println("          Candidatos a singleton: %d" % len(singletons))
                for ref_addr in singletons:
                    kind, info = classify_candidate(ref_addr)
                    println("            -> %s  [%s]" % (_hex(ref_addr), kind))
                    if info:
                        println("               vtbl=%s count=%d cap=%d buffer=%s" % (
                            _hex(info['vtbl']), info['count'],
                            info['cap'], _hex(info['buffer'])))
                    dump_object(ref_addr)

    println("")
    println("=" * 78)
    println("Fin del barrido. Copia la salida completa.")
    println("=" * 78)


main()