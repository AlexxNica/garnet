// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf_provider/categories.h"

#include <trace-engine/instrumentation.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/lib/cpuperf/events.h"

namespace cpuperf_provider {

enum EventId {
#define DEF_FIXED_EVENT(symbol, id, regnum, flags, name, description) \
  symbol = CPUPERF_MAKE_EVENT_ID(CPUPERF_UNIT_FIXED, id),
#define DEF_ARCH_EVENT(symbol, id, ebx_bit, event, umask, flags, name, description) \
  symbol = CPUPERF_MAKE_EVENT_ID(CPUPERF_UNIT_ARCH, id),
#include <zircon/device/cpu-trace/intel-pm-events.inc>

#define DEF_SKL_EVENT(symbol, id, event, umask, flags, name, description) \
  symbol = CPUPERF_MAKE_EVENT_ID(CPUPERF_UNIT_MODEL, id),
#include <zircon/device/cpu-trace/skylake-pm-events.inc>
};

#define DEF_FIXED_CATEGORY(symbol, id, name, counters...) \
  static const cpuperf_event_id_t symbol ## _events[] = { counters };
#define DEF_ARCH_CATEGORY(symbol, id, name, counters...) \
  static const cpuperf_event_id_t symbol ## _events[] = { counters };
#include "intel-pm-categories.inc"

#define DEF_SKL_CATEGORY(symbol, id, name, counters...) \
  static const cpuperf_event_id_t symbol ## _events[] = { counters };
#include "skylake-pm-categories.inc"

static const CategorySpec kCategories[] = {
  // Options
  { "cpu:os",   CategoryGroup::kOption,
    static_cast<CategoryId>(TraceOption::kOs), 0, nullptr },
  { "cpu:user", CategoryGroup::kOption,
    static_cast<CategoryId>(TraceOption::kUser), 0, nullptr },
  { "cpu:pc",   CategoryGroup::kOption,
    static_cast<CategoryId>(TraceOption::kPc), 0, nullptr },

  // Sampling rates.
  // Only one of the following is allowed.
#define DEF_SAMPLE(name, value) \
    { "cpu:" name, CategoryGroup::kSample, value, 0, nullptr }
  DEF_SAMPLE("tally", 0),
  DEF_SAMPLE("sample:100", 100),
  DEF_SAMPLE("sample:500", 500),
  DEF_SAMPLE("sample:1000", 1000),
  DEF_SAMPLE("sample:5000", 5000),
  DEF_SAMPLE("sample:10000", 10000),
  DEF_SAMPLE("sample:50000", 50000),
  DEF_SAMPLE("sample:100000", 100000),
  DEF_SAMPLE("sample:500000", 500000),
  DEF_SAMPLE("sample:1000000", 1000000),
#undef DEF_SAMPLE

  // Fixed events.
#define DEF_FIXED_CATEGORY(symbol, id, name, counters...) \
  { "cpu:" name, CategoryGroup::kFixed, id, \
    countof(symbol ## _events), &symbol ## _events[0] },
#include "intel-pm-categories.inc"

  // Architecturally specified programmable events.
#define DEF_ARCH_CATEGORY(symbol, id, name, counters...) \
  { "cpu:" name, CategoryGroup::kArch, id, \
    countof(symbol ## _events), &symbol ## _events[0] },
#include "intel-pm-categories.inc"

  // Model-specific programmable events.
#define DEF_SKL_CATEGORY(symbol, id, name, counters...) \
  { "cpu:" name, CategoryGroup::kModel, id, \
    countof(symbol ## _events), &symbol ## _events[0] },
#include "skylake-pm-categories.inc"
};


void TraceConfig::Reset() {
  is_enabled_ = false;
  trace_os_ = false;
  trace_user_ = false;
  trace_pc_ = false;
  sample_rate_ = 0;
  selected_categories_.clear();
}

void TraceConfig::Update() {
  Reset();

  // The default, if the user doesn't specify any categories, is that every
  // trace category is enabled. This doesn't work for us as the h/w doesn't
  // support enabling all counters at once. And event when multiplexing support
  // is added it may not support multiplexing everything. So watch for the
  // default case, which we have to explicitly do as the only API we have is
  // trace_is_category_enabled(), and if present apply our own default.
  size_t num_enabled_categories = 0;
  for (const auto& cat : kCategories) {
    if (trace_is_category_enabled(cat.name))
      ++num_enabled_categories;
  }
  bool is_default_case = num_enabled_categories == countof(kCategories);

  // Our default is to not trace anything: This is fairly specialized tracing
  // so we only provide it if the user explicitly requests it.
  if (is_default_case)
    return;

  bool have_something = false;
  bool have_sample_rate = false;
  bool have_programmable_category = false;

  for (const auto& cat : kCategories) {
    if (trace_is_category_enabled(cat.name)) {
      FXL_VLOG(1) << "Category " << cat.name << " enabled";
      switch (cat.group) {
        case CategoryGroup::kOption:
          switch (static_cast<TraceOption>(cat.id)) {
            case TraceOption::kOs:
              trace_os_ = true;
              break;
            case TraceOption::kUser:
              trace_user_ = true;
              break;
            case TraceOption::kPc:
              trace_pc_ = true;
              break;
          }
          break;
        case CategoryGroup::kSample:
          if (have_sample_rate) {
            FXL_LOG(ERROR) << "Only one sampling mode at a time is currenty supported";
            return;
          }
          have_sample_rate = true;
          sample_rate_ = cat.id;
          break;
        case CategoryGroup::kFixed:
          selected_categories_.insert(&cat);
          have_something = true;
          break;
        case CategoryGroup::kArch:
        case CategoryGroup::kModel:
          if (have_programmable_category) {
            FXL_LOG(ERROR) << "Only one programmable category at a time is currenty supported";
            return;
          }
          have_programmable_category = true;
          have_something = true;
          selected_categories_.insert(&cat);
          break;
      }
    }
  }

  // If neither OS,USER are specified, track both.
  if (!trace_os_ && !trace_user_) {
    trace_os_ = true;
    trace_user_ = true;
  }

  is_enabled_ = have_something;
}

bool TraceConfig::Changed(const TraceConfig& old) const {
  if (is_enabled_ != old.is_enabled_)
    return true;
  if (trace_os_ != old.trace_os_)
    return true;
  if (trace_user_ != old.trace_user_)
    return true;
  if (trace_pc_ != old.trace_pc_)
    return true;
  if (sample_rate_ != old.sample_rate_)
    return true;
  if (selected_categories_ != old.selected_categories_)
    return true;
  return false;
}

bool TraceConfig::TranslateToDeviceConfig(cpuperf_config_t* out_config) const {
  cpuperf_config_t* cfg = out_config;
  memset(cfg, 0, sizeof(*cfg));

  unsigned ctr = 0;

  for (const auto& cat : selected_categories_) {
    switch (cat->group) {
      case CategoryGroup::kFixed:
        if (ctr < countof(cfg->counters)) {
          FXL_VLOG(2) << fxl::StringPrintf("Adding fixed event id %u to trace",
                                           cat->id);
          cfg->counters[ctr++] = cpuperf::GetFixedCounterId(cat->id);
        } else {
          FXL_LOG(ERROR) << "Maximum number of counters exceeded";
          return false;
        }
        break;
      case CategoryGroup::kArch:
      case CategoryGroup::kModel:
        for (size_t i = 0; i < cat->count; ++i) {
          if (ctr < countof(cfg->counters)) {
            const char* group =
              cat->group == CategoryGroup::kArch ? "arch" : "model";
            FXL_VLOG(2) << fxl::StringPrintf("Adding %s event id %u to trace",
                                             group, cat->id);
            cfg->counters[ctr++] = cat->events[i];
          } else {
            FXL_LOG(ERROR) << "Maximum number of counters exceeded";
            return false;
          }
        }
        break;
      default:
        FXL_NOTREACHED();
    }
  }
  unsigned num_used_counters = ctr;

  uint32_t flags = 0;
  if (trace_os_)
    flags |= CPUPERF_CONFIG_FLAG_OS;
  if (trace_user_)
    flags |= CPUPERF_CONFIG_FLAG_USER;
  if (trace_pc_)
    flags |= CPUPERF_CONFIG_FLAG_PC;

  for (unsigned i = 0; i < num_used_counters; ++i) {
    cfg->rate[i] = sample_rate_;
    cfg->flags[i] = flags;
  }

  return true;
}

std::string TraceConfig::ToString() const {
  std::string result;

  if (!is_enabled_)
    return "disabled";

  if (sample_rate_ > 0) {
    result += fxl::StringPrintf("@%u", sample_rate_);
  } else {
    result += "tally";
  }

  if (trace_os_)
    result += ",os";
  if (trace_user_)
    result += ",user";
  if (trace_pc_)
    result += ",pc";

  for (const auto& cat : selected_categories_) {
    result += ",";
    result += cat->name;
  }

  return result;
}

}  // namespace cpuperf_provider
