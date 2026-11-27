// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

#pragma once

#include <idfxx/mdns>

#include <optional>
#include <string>
#include <vector>

namespace idfxx::mdns::internal {

// Fills in the instance name, service type and protocol of the answers to an
// SRV or TXT query for a named instance, from what the query asked for. The
// C library leaves all three out of an answer that holds only a TXT record
// (mdns_priv_query_result_add_txt(), mdns_querier.c). Only the fields that
// an answer lacks are filled. Answers to other queries are left unchanged.
inline void fill_instance_fields(
    std::vector<answer>& answers,
    enum record_type type,
    const std::optional<std::string>& name,
    const std::optional<std::string>& service_type,
    std::optional<enum protocol> protocol
) {
    if ((type != record_type::srv && type != record_type::txt) || !name) {
        return;
    }
    for (answer& a : answers) {
        if (!a.instance_name) {
            a.instance_name = name;
        }
        if (!a.service_type) {
            a.service_type = service_type;
        }
        if (!a.protocol) {
            a.protocol = protocol;
        }
    }
}

} // namespace idfxx::mdns::internal
