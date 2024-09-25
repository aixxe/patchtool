## patchtool

Quick and dirty companion tool for [BemaniPatcher Python script](https://github.com/drmext/BemaniPatcher) JSON output files

Converts and splits patches into [mempatch-hook format](https://github.com/djhackersdev/bemanitools/blob/master/doc/tools/mempatch-hook.md) files as defined in a YAML profile

Used in my launcher script to switch between different game versions while using the same set of patches

### Usage

The [sample](/sample) directory contains a few example files:
- JSON patch files produced by `json_to_bemanipatcher.py`
- [YAML profile](/sample/iidx30.yml) defining which patches to enable and where to write them

To generate `.mph` files, the following commands can be used:

```sh
patchtool --input=sample/2023090500-003.json --profile=sample/iidx30.yml --output=sample/
patchtool --input=sample/2023090500-010.json --profile=sample/iidx30.yml --output=sample/
patchtool --input=sample/2023090500-012.json --profile=sample/iidx30.yml --output=sample/
```

> [!TIP]
> The example below uses **[mempatcher](https://github.com/aixxe/mempatcher)** and **[spice2x](https://spice2x.github.io/)** _(24-09-21 or newer)_ to ensure patches are applied before any game code is executed. This resolves an [issue](https://github.com/spice2x/spice2x.github.io/issues/220) with specific patches that need to be applied especially early

<sub>**※ Using Bemanitools?** Switch the `-z` flag to `-B` instead</sub>
```sh
# LDJ-003 vanilla
spice64.exe -z mempatcher.dll ^
            --mempatch sample\2023090500-003.mph ^
            --mempatch sample\2023090500-003-LDJ.mph

# LDJ-012 omnimix
spice64.exe -z mempatcher.dll ^
            --mempatch sample\2023090500-012.mph ^
            --mempatch sample\2023090500-012-LDJ.mph ^
            --mempatch sample\2023090500-012-omnimix.mph

# LDJ-010 omnimix + lightning
spice64.exe -iidxtdj -z mempatcher.dll ^
            --mempatch sample\2023090500-010.mph ^
            --mempatch sample\2023090500-010-TDJ.mph ^
            --mempatch sample\2023090500-010-omnimix.mph
```

### Options

#### `-N, --no-verify`

Excludes writing the verification bytes for a patch

#### `-U, --union-all-opts`

When defined, all options of a union will be written instead of just the one in use