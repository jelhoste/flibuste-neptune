#include <catch2/catch_test_macros.hpp>
#include "neptune/core/UiState.h"
#include <filesystem>
#include <fstream>

using namespace neptune::core::uistate;

TEST_CASE ("ui state defaults differ between pointer and touch")
{
    UiState s;
    CHECK (s.modalSize (Terminal::Touch).width > s.modalSize (Terminal::Pointer).width);
}

TEST_CASE ("ui state round trip keeps each terminal separate")
{
    UiState s;
    s.setModalSize (Terminal::Pointer, { 420, 260 });
    s.setModalSize (Terminal::Touch, { 640, 400 });
    const UiState back = fromJson (toJson (s));
    CHECK (back == s);
    CHECK (back.modalSize (Terminal::Pointer) == Size { 420, 260 });
    CHECK (back.modalSize (Terminal::Touch) == Size { 640, 400 });
}

TEST_CASE ("ui state sizes are clamped and bad values fall back to defaults")
{
    UiState s;
    s.setModalSize (Terminal::Pointer, { 5, 99999 });
    CHECK (s.modalPointer == Size { minModalSide, maxModalSide });

    const UiState d;
    const UiState bad = fromJson (R"({"modal_pointer":{"width":10,"height":300},"modal_touch":"x"})");
    CHECK (bad == d);
    CHECK (fromJson ("not json") == d);
    CHECK (fromJson ("[1]") == d);
    CHECK (fromJson (R"({"modal_touch":{"width":700,"height":500}})").modalTouch == Size { 700, 500 });
}

TEST_CASE ("ui state file round trip, missing file gives defaults")
{
    const auto dir = std::filesystem::temp_directory_path() / "neptune_ui_state_test";
    std::filesystem::remove_all (dir);
    const auto file = dir / "ui_state.json";
    CHECK (loadFile (file) == UiState {});
    UiState s;
    s.setModalSize (Terminal::Touch, { 555, 333 });
    std::string err;
    REQUIRE (saveFile (file, s, &err));
    CHECK (loadFile (file) == s);
    std::filesystem::remove_all (dir);
}
