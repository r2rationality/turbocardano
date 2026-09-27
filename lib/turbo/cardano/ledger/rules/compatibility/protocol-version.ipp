/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2022-2023 Alex Sierkov (alex dot sierkov at gmail dot com)
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

// Included at class scope by lib/turbo/cardano/common/types.hpp.

        bool aggregated_rewards() const
        {
            return major > 2;
        }

        bool forgo_reward_prefilter() const
        {
            return major > 6;
        }

        bool keep_pointers() const
        {
            return major < 9;
        }

        bool bootstrap_phase() const
        {
            return major == 9;
        }
