#!/usr/bin/env python3
import argparse
import random
import string
from typing import List, Tuple

ASCII_UPPER = string.ascii_uppercase

def pick_ascii_pair(rng: random.Random) -> str:
    return rng.choice(ASCII_UPPER) + rng.choice(ASCII_UPPER)

def gen_process_block(proc_index: int,
                      rng: random.Random,
                      cpu_limit: int,
                      min_pages: int = 3,
                      max_pages: int = 10,
                      ev_offset: int = 100) -> Tuple[str, int]:
    qtd_paginas = rng.randint(min_pages, max_pages)
    physical_indices = list(range(0, qtd_paginas))
    protections = [rng.choice([0, 1, 0]) for _ in physical_indices]
    if 1 not in protections and qtd_paginas > 0:
        protections[rng.randrange(qtd_paginas)] = 1
    initial_msgs = [pick_ascii_pair(rng) for _ in physical_indices]
    total_ops = rng.randint(max(1, min(5, cpu_limit)), cpu_limit)

    lines: List[str] = []
    lines.append(f"{proc_index} {total_ops}")
    lines.append(str(qtd_paginas))
    for ef, prot, msg in zip(physical_indices, protections, initial_msgs):
        lines.append(f"{ef} {prot} {msg}")

    # linha em branco antes da sequência
    lines.append("")

    rw_pages = [ef for ef, prot in zip(physical_indices, protections) if prot == 1]
    ro_pages = [ef for ef, prot in zip(physical_indices, protections) if prot == 0]

    seq: List[str] = []
    wrote_once = False
    for i in range(total_ops):
        do_write = False
        if rw_pages:
            remaining = total_ops - i
            if remaining == 1 and not wrote_once:
                do_write = True
            else:
                do_write = rng.random() < 0.30
        if do_write:
            ef = rng.choice(rw_pages)
            ev = ef + ev_offset
            msg = pick_ascii_pair(rng)
            seq.append(f"M {ev} {msg}")
            wrote_once = True
        else:
            if rng.random() < 0.5 and ro_pages:
                ef = rng.choice(ro_pages)
            else:
                ef = rng.choice(physical_indices)
            ev = ef + ev_offset
            seq.append(f"R {ev}")

    lines.extend(seq)
    return "\n".join(lines), total_ops

def gerar_entrada(max_processos: int,
                  limite_paginas_relogio: int,
                  limite_cpu: int,
                  seed: int = 42,
                  min_pages: int = 3,
                  max_pages: int = 10,
                  ev_offset: int = 100) -> str:
    rng = random.Random(seed)
    linhas = [f"{max_processos} {limite_paginas_relogio} {limite_cpu}", ""]
    for p in range(max_processos):
        bloco, _ = gen_process_block(p, rng, limite_cpu,
                                     min_pages=min_pages,
                                     max_pages=max_pages,
                                     ev_offset=ev_offset)
        linhas.append(bloco)
        if p != max_processos - 1:  # separa processos com uma linha em branco
            linhas.append("")
    return "\n".join(linhas) + "\n"

def main():
    ap = argparse.ArgumentParser(description="Gerador de arquivo de entrada para simulação de memória/CPU.")
    ap.add_argument("--processos", type=int, default=2)
    ap.add_argument("--relogio", type=int, default=5)
    ap.add_argument("--cpu", type=int, default=10)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--min-pag", type=int, default=3)
    ap.add_argument("--max-pag", type=int, default=10)
    ap.add_argument("--offset", type=int, default=100)
    ap.add_argument("--saida", type=str, default="entrada.txt")
    args = ap.parse_args()

    texto = gerar_entrada(
        max_processos=args.processos,
        limite_paginas_relogio=args.relogio,
        limite_cpu=args.cpu,
        seed=args.seed,
        min_pages=args.min_pag,
        max_pages=args.max_pag,
        ev_offset=args.offset,
    )

    with open(args.saida, "w", encoding="utf-8") as f:
        f.write(texto)

    print(f"Arquivo gerado: {args.saida}")

if __name__ == "__main__":
    main()
