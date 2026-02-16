#include <erpl_adt/adt/bw_context_headers.hpp>

namespace erpl_adt {

void ApplyBwContextHeaders(const BwContextHeaders& context, HttpHeaders& headers) {
    if (context.transport_lock_holder.has_value() &&
        !context.transport_lock_holder->empty()) {
        headers["Transport-Lock-Holder"] = *context.transport_lock_holder;
    }
    if (context.foreign_objects.has_value() &&
        !context.foreign_objects->empty()) {
        headers["Foreign-Objects"] = *context.foreign_objects;
    }
    if (context.foreign_object_locks.has_value() &&
        !context.foreign_object_locks->empty()) {
        headers["Foreign-Object-Locks"] = *context.foreign_object_locks;
    }
    if (context.foreign_correction_number.has_value() &&
        !context.foreign_correction_number->empty()) {
        headers["Foreign-Correction-Number"] = *context.foreign_correction_number;
    }
    if (context.foreign_package.has_value() &&
        !context.foreign_package->empty()) {
        headers["Foreign-Package"] = *context.foreign_package;
    }
}

}  // namespace erpl_adt
