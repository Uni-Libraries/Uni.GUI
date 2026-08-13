#include <uni/gui/nodes/property.h>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

using namespace Uni::GUI::Nodes;

void Expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

PropertyValue Fixture() {
    return PropertyValue::Object{
        {"enabled", true},
        {"inputs", PropertyValue::Array{
            PropertyValue::Object{
                {"name", "left"},
                {"source", PropertyReference{.type = "asset", .value = "images/left"}},
            },
            PropertyValue::Object{
                {"name", "right"},
                {"offset", Vec2{2.0f, 3.0f}},
            },
        }},
        {"optional", PropertyNull{}},
    };
}

void TestRecursiveValues() {
    const PropertyValue value = Fixture();
    const auto& object = value.Get<PropertyValue::Object>();
    const auto& inputs = object.at("inputs").Get<PropertyValue::Array>();
    Expect(inputs.size() == 2 &&
               inputs[0].Get<PropertyValue::Object>().at("name").Get<std::string>() == "left" &&
               inputs[1].Get<PropertyValue::Object>().at("offset").Get<Vec2>() == Vec2{2.0f, 3.0f},
           "Recursive values must expose the same alternatives at every depth");
}

void TestPropertyPaths() {
    PropertyBag properties{{"configuration", Fixture()}};
    const PropertyPath name{"configuration", std::string{"inputs"}, std::size_t{1}, std::string{"name"}};
    const auto* found = FindProperty(properties, name);
    Expect(found != nullptr && found->Get<std::string>() == "right", "Property paths must resolve mixed object and array segments");

    auto replaced = SetProperty(properties, name, PropertyValue{"renamed"});
    Expect(replaced && FindProperty(*replaced, name)->Get<std::string>() == "renamed",
           "Property paths must replace one nested value without changing the root API");

    const PropertyPath source{"configuration", std::string{"inputs"}, std::size_t{0}, std::string{"source"}};
    auto erased = SetProperty(*replaced, source, std::nullopt);
    Expect(erased && FindProperty(*erased, source) == nullptr && FindProperty(*erased, name) != nullptr,
           "Property paths must erase only the addressed nested value");

    auto invalid_array_erase = SetProperty(
        properties,
        PropertyPath{"configuration", std::string{"inputs"}, std::size_t{0}},
        std::nullopt);
    Expect(!invalid_array_erase && invalid_array_erase.error().code == ErrorCode::InvalidArgument,
           "Array element removal must replace the containing array to keep set and undo semantics exact");

    Expect(PropertyPath{"configuration", std::string{"inputs"}}.IsPrefixOf(name),
           "Property path prefix matching must support nested schema configuration");
}

void TestValidationAndLimits() {
    const PropertyValue valid = Fixture();
    Expect(ValidatePropertyValue(valid).has_value(), "A complete recursive property must validate");

    const PropertyValue invalid = PropertyValue::Array{std::numeric_limits<double>::infinity()};
    auto finite = ValidatePropertyValue(invalid);
    Expect(!finite && finite.error().code == ErrorCode::InvalidArgument, "Nested non-finite numbers must be rejected");

    PropertyLimits depth_limit;
    depth_limit.max_depth = 2;
    auto depth = ValidatePropertyValue(valid, depth_limit);
    Expect(!depth && depth.error().code == ErrorCode::SizeLimitExceeded, "Recursive values must honor semantic depth limits");

    PropertyLimits value_limit;
    value_limit.max_values = 3;
    auto values = ValidatePropertyValue(valid, value_limit);
    Expect(!values && values.error().code == ErrorCode::SizeLimitExceeded, "Recursive values must honor semantic value limits");

    PropertyLimits bag_limit;
    bag_limit.max_values = 1;
    auto bag = ValidatePropertyBag(PropertyBag{{"left", true}, {"right", false}}, bag_limit);
    Expect(!bag && bag.error().code == ErrorCode::SizeLimitExceeded, "Property bag limits must aggregate every root value");

    Expect(EstimatePropertyBytes(valid) >= sizeof(PropertyValue), "Property memory estimates must include the root value");
}

} // namespace

int main() {
    TestRecursiveValues();
    TestPropertyPaths();
    TestValidationAndLimits();
    return EXIT_SUCCESS;
}
