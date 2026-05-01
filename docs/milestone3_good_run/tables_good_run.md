# Milestone 3 good run — summary tables

Source: Stage 5 benchmark on Adroit (`-r 3`). Throughput = **median** of the three mixed-throughput columns per row.

## 10% insert / 90% lookup

| Dataset | LIPP Mops/s | Adv Mops/s | DPGM Mops/s | LIPP GB | Adv GB | DPGM GB |
|---------|------------:|-----------:|------------:|--------:|-------:|--------:|
| books | 5.956 | 5.267 | 0.908 | 11.82 | 11.82 | 1.70 |
| fb | 6.063 | 5.362 | 0.777 | 12.70 | 12.69 | 1.71 |
| osmc | 3.343 | 3.200 | 0.875 | 20.60 | 20.73 | 1.70 |

## 90% insert / 10% lookup

| Dataset | LIPP Mops/s | Adv Mops/s | DPGM Mops/s | LIPP GB | Adv GB | DPGM GB |
|---------|------------:|-----------:|------------:|--------:|-------:|--------:|
| books | 3.049 | 3.866 | 2.711 | 11.75 | 11.69 | 1.70 |
| fb | 2.618 | 3.909 | 2.798 | 12.66 | 12.54 | 1.71 |
| osmc | 2.003 | 3.270 | 2.517 | 20.41 | 20.29 | 1.70 |
