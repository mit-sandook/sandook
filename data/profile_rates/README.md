# Per-SSD profile rates (optional)

`profile_disk.sh` can optionally load per-SSD profiling rates from CSV files in
this directory.

## File naming

- `${SERIAL}_profile_rates.csv`

Example:

- `S5CVNA0N101050_profile_rates.csv`

## File format

CSV with a header row:

```csv
set_rate,mpps
0,1.1
300,1.0
500,0.3
700,0.3
1000,0.5
```

If the file is missing (or has no valid data rows), `profile_disk.sh` falls back
to its built-in defaults.


