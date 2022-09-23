# README #

A high performace multi threaded and non-synchronized reader of OSM files (OSM, PBF, OSC).

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
