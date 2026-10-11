// Copyright (C) 2020-2025 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include <glaze/json.hpp>
#include <lexy/input/file.hpp>
#include <span>

bool json_glaze(const lexy::buffer<lexy::utf8_encoding>& input)
{
    return !glz::validate_json(std::span(input.data(), input.size()));
}

