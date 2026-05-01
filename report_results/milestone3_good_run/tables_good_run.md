# Milestone 3 — tables (one per figure)

Each section matches one bar chart in this folder (`*.png`). Mixed workload benchmark (Adroit, Stage 5 hybrid).

## `10pct_insert_books_throughput.png`

**books (100M uint64)** — 10% insert / 90% lookup

| Index | Throughput (M ops/s) |
|-------|---------------------:|
| LIPP | 5.956 |
| HybridPGMLippAdv | 5.267 |
| DynamicPGM | 0.908 |

## `10pct_insert_books_index_size.png`

**books (100M uint64)** — 10% insert / 90% lookup

| Index | Size (GB) | Size (bytes) |
|-------|----------:|-------------:|
| LIPP | 11.82 | 11,818,525,760 |
| HybridPGMLippAdv | 11.82 | 11,816,089,464 |
| DynamicPGM | 1.70 | 1,703,429,848 |

## `10pct_insert_fb_throughput.png`

**Facebook (100M uint64)** — 10% insert / 90% lookup

| Index | Throughput (M ops/s) |
|-------|---------------------:|
| LIPP | 6.063 |
| HybridPGMLippAdv | 5.362 |
| DynamicPGM | 0.777 |

## `10pct_insert_fb_index_size.png`

**Facebook (100M uint64)** — 10% insert / 90% lookup

| Index | Size (GB) | Size (bytes) |
|-------|----------:|-------------:|
| LIPP | 12.70 | 12,700,946,864 |
| HybridPGMLippAdv | 12.69 | 12,692,726,328 |
| DynamicPGM | 1.71 | 1,705,226,028 |

## `10pct_insert_osmc_throughput.png`

**OSMC (100M uint64)** — 10% insert / 90% lookup

| Index | Throughput (M ops/s) |
|-------|---------------------:|
| LIPP | 3.343 |
| HybridPGMLippAdv | 3.200 |
| DynamicPGM | 0.875 |

## `10pct_insert_osmc_index_size.png`

**OSMC (100M uint64)** — 10% insert / 90% lookup

| Index | Size (GB) | Size (bytes) |
|-------|----------:|-------------:|
| LIPP | 20.60 | 20,602,887,232 |
| HybridPGMLippAdv | 20.73 | 20,728,431,784 |
| DynamicPGM | 1.70 | 1,701,834,168 |

## `90pct_insert_books_throughput.png`

**books (100M uint64)** — 90% insert / 10% lookup

| Index | Throughput (M ops/s) |
|-------|---------------------:|
| LIPP | 3.049 |
| HybridPGMLippAdv | 3.866 |
| DynamicPGM | 2.711 |

## `90pct_insert_books_index_size.png`

**books (100M uint64)** — 90% insert / 10% lookup

| Index | Size (GB) | Size (bytes) |
|-------|----------:|-------------:|
| LIPP | 11.75 | 11,753,613,200 |
| HybridPGMLippAdv | 11.69 | 11,691,092,636 |
| DynamicPGM | 1.70 | 1,700,011,168 |

## `90pct_insert_fb_throughput.png`

**Facebook (100M uint64)** — 90% insert / 10% lookup

| Index | Throughput (M ops/s) |
|-------|---------------------:|
| LIPP | 2.618 |
| HybridPGMLippAdv | 3.909 |
| DynamicPGM | 2.798 |

## `90pct_insert_fb_index_size.png`

**Facebook (100M uint64)** — 90% insert / 10% lookup

| Index | Size (GB) | Size (bytes) |
|-------|----------:|-------------:|
| LIPP | 12.66 | 12,656,662,928 |
| HybridPGMLippAdv | 12.54 | 12,543,284,404 |
| DynamicPGM | 1.71 | 1,705,154,448 |

## `90pct_insert_osmc_throughput.png`

**OSMC (100M uint64)** — 90% insert / 10% lookup

| Index | Throughput (M ops/s) |
|-------|---------------------:|
| LIPP | 2.003 |
| HybridPGMLippAdv | 3.270 |
| DynamicPGM | 2.517 |

## `90pct_insert_osmc_index_size.png`

**OSMC (100M uint64)** — 90% insert / 10% lookup

| Index | Size (GB) | Size (bytes) |
|-------|----------:|-------------:|
| LIPP | 20.41 | 20,408,967,088 |
| HybridPGMLippAdv | 20.29 | 20,291,323,864 |
| DynamicPGM | 1.70 | 1,701,839,508 |
