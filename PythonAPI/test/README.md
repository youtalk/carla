# PythonAPI Tests

Suites in this directory run with [nose2](https://docs.nose2.io/) from
`PythonAPI/test/`:

- `unit/` - pure-Python tests, no simulator needed.
- `smoke/` - requires a running simulator; see `smoke_test_list.txt` for the
  canonical list.
- `API/` - heavier integration tests.

```sh
python -m nose2 -v unit
python -m nose2 -v smoke.test_client
```

## Smoke tests

The smoke suite connects to `localhost:3654` by default. Set
`CARLA_SMOKE_HOST` / `CARLA_SMOKE_PORT` when the simulator is not on
localhost:3654, e.g.:

```sh
CARLA_SMOKE_PORT=2000 python -m nose2 -v smoke.test_ros2
```
