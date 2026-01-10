#include <dotvm/dotvm.hpp>
#include <cstdio>

int main() {
    using namespace dotvm::core;

    // Quick demonstration of the register file
    RegisterFile rf;

    // R0 is hardwired to zero
    rf.write(0, Value::from_int(42));
    auto r0 = rf.read(0);
    std::printf("R0 (should be 0.0): %f\n", r0.as_float());

    // Regular register operations
    rf[1] = Value::from_int(100);
    rf[2] = Value::from_float(3.14159);
    rf[3] = Value::from_bool(true);
    rf[4] = Value::from_handle(42, 1);

    std::printf("R1 (int): %ld\n", static_cast<long>(rf.read(1).as_integer()));
    std::printf("R2 (float): %f\n", rf.read(2).as_float());
    std::printf("R3 (bool): %s\n", rf.read(3).as_bool() ? "true" : "false");

    auto h = rf.read(4).as_handle();
    std::printf("R4 (handle): index=%u, generation=%u\n", h.index, h.generation);

    std::printf("\nRegisterFile size: %zu bytes\n", RegisterFile::byte_size());
    std::printf("Value size: %zu bytes\n", sizeof(Value));

    return 0;
}
