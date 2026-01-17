#include <numbers>
#include <print>

#include <dotvm/core/register_file.hpp>
#include <dotvm/core/value.hpp>

int main() {
    using namespace dotvm::core;

    // Quick demonstration of the register file
    RegisterFile rf;

    // R0 is hardwired to zero
    rf.write(0, Value::from_int(42));
    auto r0 = rf.read(0);
    std::println("R0 (should be 0.0): {:f}", r0.as_float());

    // Regular register operations
    rf[1] = Value::from_int(100);
    rf[2] = Value::from_float(std::numbers::pi);
    rf[3] = Value::from_bool(true);
    rf[4] = Value::from_handle(42, 1);

    std::println("R1 (int): {}", rf.read(1).as_integer());
    std::println("R2 (float): {:f}", rf.read(2).as_float());
    std::println("R3 (bool): {}", rf.read(3).as_bool());

    auto h = rf.read(4).as_handle();
    std::println("R4 (handle): index={}, generation={}", h.index, h.generation);

    std::println("\nRegisterFile size: {} bytes", RegisterFile::byte_size());
    std::println("Value size: {} bytes", sizeof(Value));

    return 0;
}
