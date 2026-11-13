/* This file is part of TurboCardano project: https://github.com/r2rationality/turbocardano
 * Copyright (c) 2024-2026 R2 Rationality OÜ (info at r2rationality dot com)
 * License: https://github.com/r2rationality/turbocardano/blob/main/LICENSE */

#include <turbo/cardano/dijkstra/block.hpp>

namespace turbo::cardano::dijkstra {
    void header_version_info_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2).uint(highest_supported_major_version).uint(self_reported_software_tag);
    }

    void eb_announcement_t::to_cbor(era_encoder &enc) const
    {
        enc.array(2).bytes(hash).uint(size);
    }

    void block_header::to_cbor(era_encoder &enc) const
    {
        enc.raw_cbor(data_raw());
    }
}
