# README #

A high performace multi threaded and non-synchronized reader of OSM files (OSM, PBF, OSC).

### How to test how fast it is? ###

This is the result of running the statistics on a dual Xeon E5-2699, 72 cores total. I am curious what are the results on your system.
Run:

    ```sh
    $ time ./statistics /mnt/maps/planet-220905.osm.pbf 1
    /mnt/maps/planet-220905.osm.pbf
    reading metadata
    running on 72 threads
    file size is 69,968,288,348 bytes
    reading block 38,478 offset 69,967,313,594
    block work queue has 38,479 items
    left: -180.000000000
    right: 180.000000000
    top: 90.000000000
    bottom: -90.000000000
    required feature: OsmSchema-V0.6
    required feature: DenseNodes
    optional feature: Has_Metadata
    optional feature: Sort.Type_then_ID
    writing_program: planet-dump-ng 1.2.4
    source: http://www.openstreetmap.org/api/0.6
    osmosis_replication_timestamp: 1662336000 "2022-09-05 00:00:00 GMT"
    nodes: 7,894,460,004
    ways: 884,986,817
    relations: 10,199,553
    max nodes per block: 16,000
    max node tags per block: 192,923
    max ways per block: 8,000
    max way tags per block: 144,722
    max way nodes per block: 833,428
    max relations per block: 500
    max relation tags per block: 12,213
    max relation members per block: 64,048
    max node timestamp: 2022-09-04 23:59:55 GMT
    max way timestamp: 2022-09-04 23:59:55 GMT
    max relation timestamp: 2022-09-04 23:59:28 GMT
    max file block index: 38,478
    nodes with tags: 199,023,997
    ways with tags: 870,315,135
    relations with tags: 10,199,370
    max node id: 10,000,751,104
    max way id: 1,091,967,263
    max relation id: 14,543,577

    real    0m28.215s
    user    30m23.402s
    sys     0m18.354s
    ```
    
### How do I get set up? ###

* Summary of set up

    Install:
    1) cmake > 3.5
    2) make (or another build system, like ninja)
    3) c++20 compiler


* Dependencies

    This library depends on EXPAT and ZLIB


* Configuration

    Run:
    
    ```sh
    # linux
    cmake -S. -Bbuild && cmake --build build --target all --parallel $(nproc)
    # mac
    # cmake -S. -Bbuild && cmake --build build --target all --parallel $(sysctl -n hw.ncpu)
    ```


* How to run tests

    Enable tests by doing:

    ```sh
    # linux
    cmake -S. -Bbuild -DINPUTOSM_INTEGRATION_TESTS=ON
    ```

* Deployment instructions

    Install the library with:

    ```sh
    cmake -S. -Bbuild && cmake --build build --target install
    ```

### Contribution guidelines ###

* Writing tests

    Add tests in the tests folder.

### Who do I talk to? ###

    Stefan Karschti
