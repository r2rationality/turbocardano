#pragma once
/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include "const-iterator.hpp"

namespace turbo::storage {
    struct const_reverse_iterator {
        const_reverse_iterator() =delete;

        const_reverse_iterator(const const_iterator &base) noexcept:
            _base { base }
        {
        }

        const_reverse_iterator(const const_reverse_iterator &o) noexcept:
            _base { o._base }
        {
        }

        const_reverse_iterator &operator=(const const_reverse_iterator &o)
        {
            _base = o._base;
            return *this;
        }

        bool operator==(const const_reverse_iterator &o) const
        {
            return _base == o._base;
        }

        const block_info &operator*() const
        {
            auto tmp = _base;
            return *(--tmp);
        }

        const block_info *operator->() const
        {
            return &operator*();
        }

        const_reverse_iterator &operator--()
        {
            ++_base;
            return *this;
        }

        const_reverse_iterator &operator++()
        {
            --_base;
            return *this;
        }
    private:
        const_iterator _base;
    };
}
