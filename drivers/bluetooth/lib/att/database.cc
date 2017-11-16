// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "database.h"

#include <algorithm>

#include "lib/fxl/logging.h"

namespace btlib {
namespace att {
namespace {

bool StartLessThan(const AttributeGrouping& grp, const Handle handle) {
  return grp.start_handle() < handle;
}

bool EndLessThan(const AttributeGrouping& grp, const Handle handle) {
  return grp.end_handle() < handle;
}

}  // namespace

Database::Database(Handle range_start, Handle range_end)
    : range_start_(range_start), range_end_(range_end) {
  FXL_DCHECK(range_start_ < range_end_);
  FXL_DCHECK(range_start_ >= kHandleMin);
  FXL_DCHECK(range_end_ <= kHandleMax);
}

AttributeGrouping* Database::NewGrouping(const common::UUID& group_type,
                                         size_t attr_count,
                                         const common::ByteBuffer& decl_value) {
  // This method looks for a |pos| before which to insert the new grouping.
  Handle start_handle;
  decltype(groupings_)::iterator pos;

  if (groupings_.empty()) {
    if (range_end_ - range_start_ < attr_count)
      return nullptr;

    start_handle = range_start_;
    pos = groupings_.end();
  } else if (groupings_.front().start_handle() - range_start_ > attr_count) {
    // There is room at the head of the list.
    start_handle = range_start_;
    pos = groupings_.begin();
  } else if (range_end_ - groupings_.back().end_handle() > attr_count) {
    // There is room at the tail end of the list.
    start_handle = groupings_.back().end_handle() + 1;
    pos = groupings_.end();
  } else {
    // Linearly search for a gap that fits the new grouping.
    // TODO(armansito): This is suboptimal for long running cases where the
    // database is fragmented. Think about using a better algorithm.

    auto prev = groupings_.begin();
    pos = prev;
    pos++;

    for (; pos != groupings_.end(); ++pos, ++prev) {
      size_t next_avail = pos->start_handle() - prev->end_handle() - 1;
      if (attr_count < next_avail)
        break;
    }

    if (pos == groupings_.end()) {
      FXL_VLOG(1) << "att: Attribute database is out of space!";
      return nullptr;
    }

    start_handle = prev->end_handle() + 1;
  }

  auto iter =
      groupings_.emplace(pos, group_type, start_handle, attr_count, decl_value);
  FXL_DCHECK(iter != groupings_.end());

  return &*iter;
}

bool Database::RemoveGrouping(Handle start_handle) {
  auto iter = std::lower_bound(groupings_.begin(), groupings_.end(),
                               start_handle, StartLessThan);

  if (iter == groupings_.end() || iter->start_handle() != start_handle)
    return false;

  groupings_.erase(iter);
  return true;
}

ErrorCode Database::FindInformation(Handle start_handle,
                                    Handle end_handle,
                                    uint16_t max_payload_size,
                                    std::list<const Attribute*>* out_results) {
  FXL_DCHECK(out_results);

  // Should be large enough to accomodate at least one entry with a non-empty
  // value (NOTE: in production this will be at least equal to
  // l2cap::kMinLEMTU). Smaller values are allowed for unit tests.
  FXL_DCHECK(max_payload_size > sizeof(InformationData128));

  if (start_handle == kInvalidHandle || start_handle > end_handle)
    return ErrorCode::kInvalidHandle;

  std::list<const Attribute*> results;

  // Find the first overlapping grouping.
  auto iter = std::lower_bound(groupings_.begin(), groupings_.end(),
                               start_handle, EndLessThan);
  if (iter == groupings_.end() || iter->start_handle() > end_handle)
    return ErrorCode::kAttributeNotFound;

  size_t uuid_size;
  size_t entry_size;
  bool done = false;
  for (; iter != groupings_.end() && !done; ++iter) {
    if (iter->start_handle() > end_handle)
      break;

    if (!iter->active() || !iter->complete())
      continue;

    // Search the attributes in the current grouping that are within the
    // requested range.
    Handle search_start = std::max(iter->start_handle(), start_handle);
    Handle search_end = std::min(iter->end_handle(), end_handle);

    size_t index = search_start - iter->start_handle();
    size_t end_index = search_end - iter->start_handle();
    FXL_DCHECK(end_index < iter->attributes().size());

    for (; index <= end_index && !done; ++index) {
      const auto& attr = iter->attributes()[index];
      size_t compact_size = attr.type().CompactSize(false /* allow_32bit */);

      if (results.empty()) {
        // The compact size of the first attribute type determines |uuid_size|.
        uuid_size = compact_size;
        entry_size = std::min(uuid_size + sizeof(Handle),
                              static_cast<size_t>(max_payload_size));
      } else if (compact_size != uuid_size || entry_size > max_payload_size) {
        done = true;
        break;
      }

      results.push_back(&attr);
      max_payload_size -= entry_size;
    }
  }

  if (results.empty())
    return ErrorCode::kAttributeNotFound;

  *out_results = std::move(results);
  return ErrorCode::kNoError;
}

ErrorCode Database::ReadByGroupType(
    Handle start_handle,
    Handle end_handle,
    const common::UUID& group_type,
    uint16_t max_payload_size,
    std::list<AttributeGrouping*>* out_results) {
  FXL_DCHECK(out_results);

  // Should be large enough to accomodate at least one entry with a non-empty
  // value (NOTE: in production this will be at least equal to
  // l2cap::kMinLEMTU). Smaller values are allowed for unit tests.
  FXL_DCHECK(max_payload_size > sizeof(AttributeGroupDataEntry));

  if (start_handle == kInvalidHandle || start_handle > end_handle)
    return ErrorCode::kInvalidHandle;

  std::list<AttributeGrouping*> results;

  // Find the first grouping with start >= |start_handle|. The group type and
  // the resulting value is always obtained from the first handle of an
  // attribute grouping.
  auto iter = std::lower_bound(groupings_.begin(), groupings_.end(),
                               start_handle, StartLessThan);
  if (iter == groupings_.end() || iter->start_handle() > end_handle)
    return ErrorCode::kAttributeNotFound;

  // "If the attributes with the requested type within the handle range have
  // attribute values with different lengths, then multiple Read By Group Type
  // Requests must be made." (see Vol 3, Part F, 3.4.4.9).
  //
  // |value_length| is determined by the first match.
  size_t value_size;
  size_t entry_size;
  for (; iter != groupings_.end(); ++iter) {
    // Exit the loop if the grouping is out of range.
    if (iter->start_handle() > end_handle)
      break;

    if (!iter->active() || !iter->complete())
      continue;

    if (iter->group_type() != group_type)
      continue;

    // TODO(armansito): Compare against actual connection security level here.
    // We currently do not allow security at the service declaration level, so
    // groupings are always readable.
    FXL_DCHECK(iter->attributes()[0].read_reqs().allowed_without_security());

    // The first attribute determines |value_size| and |entry_size|.
    if (results.empty()) {
      // The size of the complete first attribute value. All other matching
      // attributes need to have this size.
      value_size = iter->decl_value().size();

      // The actual size of the attribute group data entry that this attribute
      // would produce. This is both bounded by |max_payload_size| and the
      // maximum value size that a Read By Group Type Response can accomodate.
      entry_size = std::min(
          value_size, static_cast<size_t>(kMaxReadByGroupTypeValueLength));
      entry_size = std::min(entry_size + sizeof(AttributeGroupDataEntry),
                            static_cast<size_t>(max_payload_size));
    } else if (iter->decl_value().size() != value_size ||
               entry_size > max_payload_size) {
      // Stop the search if a matching attribute has a different value size than
      // the first attribute or if it wouldn't fit within the payload.
      break;
    }

    results.push_back(&*iter);
    max_payload_size -= entry_size;
  }

  if (results.empty())
    return ErrorCode::kAttributeNotFound;

  *out_results = std::move(results);
  return ErrorCode::kNoError;
}

ErrorCode Database::ReadByType(Handle start_handle,
                               Handle end_handle,
                               const common::UUID& type,
                               uint16_t max_payload_size,
                               std::list<const Attribute*>* out_results) {
  FXL_DCHECK(out_results);

  // Should be large enough to accomodate at least one entry with a non-empty
  // value (NOTE: in production this will be at least equal to
  // l2cap::kMinLEMTU). Smaller values are allowed for unit tests.
  FXL_DCHECK(max_payload_size > sizeof(AttributeData));

  if (start_handle == kInvalidHandle || start_handle > end_handle)
    return ErrorCode::kInvalidHandle;

  std::list<const Attribute*> results;

  // Find the first grouping that overlaps the requested range (i.e.
  // grouping->end_handle() >= |start_handle|).
  auto iter = std::lower_bound(groupings_.begin(), groupings_.end(),
                               start_handle, EndLessThan);
  if (iter == groupings_.end() || iter->start_handle() > end_handle)
    return ErrorCode::kAttributeNotFound;

  // |value_size| is the size of the attribute value contained in each resulting
  // AttributeData entry. |entry_size| = |value_size| + sizeof(Handle) (i.e. the
  // exact size of each AttributeData entry). We track these separately to avoid
  // recalculating one every time.
  size_t value_size;
  size_t entry_size;

  // Used by the inner loop to exit the outer loop in the case of early
  // termination.
  bool done = false;
  for (; iter != groupings_.end() && !done; ++iter) {
    // Exit the loop if the grouping is out of range.
    if (iter->start_handle() > end_handle)
      break;

    // Skip inactive or incomplete groupings.
    if (!iter->active() || !iter->complete())
      continue;

    // Search the attributes in the current grouping that are within the
    // requested range.
    Handle search_start = std::max(iter->start_handle(), start_handle);
    Handle search_end = std::min(iter->end_handle(), end_handle);

    size_t index = search_start - iter->start_handle();
    size_t end_index = search_end - iter->start_handle();
    FXL_DCHECK(end_index < iter->attributes().size());

    for (; index <= end_index && !done; ++index) {
      const auto& attr = iter->attributes()[index];
      if (attr.type() != type)
        continue;

      // TODO(armansito): Compare against the actual connection security level
      // here. For now allow only attributes that require no security.
      if (!attr.read_reqs().allowed_without_security()) {
        // Return error if this attribute would cause an error and this is the
        // first grouping that matched.
        //
        // TODO(armansito): Return correct error based on security check.
        if (results.empty())
          return ErrorCode::kReadNotPermitted;

        // Terminate the request with what has been found.
        done = true;
        break;
      }

      // The first result determines |value_size| and |entry_size|.
      if (results.empty()) {
        // If this is a static attribute (i.e. its value is present in the
        // database):
        if (attr.value()) {
          // The size of the complete first attribute value. All other matching
          // attributes need to have this size.
          value_size = attr.value()->size();

          // The actual size of the attribute data entry that this attribute
          // would produce. This is both bounded by |max_payload_size| and the
          // maximum value size that a Read By Type Response can accomodate.
          entry_size = std::min(value_size,
                                static_cast<size_t>(kMaxReadByTypeValueLength));
          entry_size = std::min(entry_size + sizeof(AttributeData),
                                static_cast<size_t>(max_payload_size));
        } else {
          // If the first value is dynamic then this is the only attribute that
          // this call will return. No need to calculate |entry_size|. The entry
          // will be added to |results| below.
          done = true;
        }
      } else if (!attr.value() || attr.value()->size() != value_size ||
                 entry_size > max_payload_size) {
        // Stop the search and exclude this attribute because:
        // a. we ran into a dynamic value in a result that contains static
        //    values, OR
        // b. the matching attribute has a different value size than the first
        //    attribute, OR
        // c. there is no remaning space in the response PDU.
        done = true;
        break;
      }

      results.push_back(&attr);
      max_payload_size -= entry_size;
    }
  }

  if (results.empty())
    return ErrorCode::kAttributeNotFound;

  *out_results = std::move(results);
  return ErrorCode::kNoError;
}

}  // namespace att
}  // namespace btlib
