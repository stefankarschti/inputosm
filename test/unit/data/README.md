# PBF test fixtures

The fixtures contain synthetic OSM data without external data or credentials.
The generator uses fixed IDs, coordinates, text, and metadata.
Tests read the checked-in files without Python or osmium.

| File | Encoding | Nodes | Ways | Relations |
| --- | --- | --- | --- | --- |
| `comprehensive.osm.pbf` | Dense nodes and Zlib Blobs | 17,005 | 12 | 4 |
| `ordinary.osm.pbf` | Ordinary nodes and raw Blobs | 7 | 12 | 4 |

The files cover multiple blocks, 64-bit IDs, negative coordinate deltas, negative reference deltas, and complete metadata.
They include empty values, Unicode text, 270 extra tags on the first node, and a way that references every node.
The relation members include nodes, ways, and relations with empty and Unicode roles.
All references resolve within each file.

The generation tool versions were osmium 1.19.1 and libosmium 2.23.1.
The generator creates temporary XML input and converts it once for each output format.

To recreate the fixtures, run this command from the repository root:

```sh
python3 test/unit/data/generate_pbf_fixture.py
```

To check reference completeness, run these commands:

```sh
osmium check-refs -r test/unit/data/comprehensive.osm.pbf
osmium check-refs -r test/unit/data/ordinary.osm.pbf
```

The C++ test specifies expected entity values independently of the decoder.
It also creates temporary PBF messages for field order, split arrays, string table layout, and block parameters.
Other temporary cases cover long strings, storage growth, malformed input, cancellation, and callback exceptions.
The helper in `../pbf_test_data.h` supplies only test encoding operations.
It does not use the library decoder to construct expected values.
