// main.cpp — CSoT'26 Week 1 entry point
//
// Usage:
//   ./runner <path/to/strategy.so> <path/to/ticks.csv>
//
// Example:
//   ./runner build/spec_strategy.so data/tiny.csv
//   ./runner build/spec_strategy.so data/public.csv

#include "engine.hpp"      
#include "strategy.hpp"    

#include <dlfcn.h>         // dlopen, dlsym, dlclose
#include <iostream>
#include <cstdlib>         

int main(int argc, char* argv[]) {

    if (argc != 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <strategy.so> <ticks.csv>\n"
                  << "Example: " << argv[0]
                  << " build/spec_strategy.so data/tiny.csv\n";
        return EXIT_FAILURE;
    }

    const char* so_path  = argv[1];
    const char* csv_path = argv[2];

    // -----------------------------------------------------------------------
    // 2. Load the strategy shared library
    //
    //    RTLD_NOW  — resolve all symbols immediately so a bad .so fails here
    //                rather than crashing mid-replay.
    //    RTLD_LOCAL — don't pollute the global symbol namespace.
    // -----------------------------------------------------------------------
    void* handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        std::cerr << "ERROR: dlopen(\"" << so_path << "\") failed:\n"
                  << "  " << dlerror() << "\n"
                  << "Did you build the strategy .so first?\n"
                  << "  cmake --build build --target spec_strategy\n";
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // 3. Resolve the factory symbol
    //
    //    The ABI requires exactly:
    //      extern "C" csot::Strategy* create_strategy();
    // -----------------------------------------------------------------------
    using FactoryFn = csot::Strategy*(*)();

    // Clear any stale dlerror state before calling dlsym.
    dlerror();
    auto create_strategy = reinterpret_cast<FactoryFn>(
        dlsym(handle, "create_strategy")
    );

    const char* dlsym_err = dlerror();
    if (dlsym_err) {
        std::cerr << "ERROR: dlsym(\"create_strategy\") failed:\n"
                  << "  " << dlsym_err << "\n"
                  << "Make sure your .so exports:\n"
                  << "  extern \"C\" csot::Strategy* create_strategy();\n";
        dlclose(handle);
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // 4. Instantiate strategy and run the engine
    // -----------------------------------------------------------------------
    csot::Strategy* strategy = create_strategy();
    if (!strategy) {
        std::cerr << "ERROR: create_strategy() returned nullptr\n";
        dlclose(handle);
        return EXIT_FAILURE;
    }

    {
        csot::Engine engine(csv_path, strategy);
        engine.run();
        engine.print_stats(std::cout);
    }   // engine destructor fires before we delete strategy

    // -----------------------------------------------------------------------
    // 5. Cleanup
    //    The ABI says the engine owns the Strategy* and deletes it.
    //    If your Engine destructor already does `delete strategy_`, remove
    //    the explicit delete below to avoid a double-free.
    // -----------------------------------------------------------------------
    delete strategy;
    dlclose(handle);

    return EXIT_SUCCESS;
}