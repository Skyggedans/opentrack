/* Copyright (c) 2023, Khoa Nguyen <khoanguyen@3forcom.com>

 * Permission to use, copy, modify, and/or distribute this
 * software for any purpose with or without fee is hereby granted,
 * provided that the above copyright notice and this permission
 * notice appear in all copies.
 */

#include "tobii.h"
#include "compat/math-imports.hpp"

#include <QMutexLocker>

static constexpr double rad_to_deg = 180.0 * M_1_PI;
static constexpr double mm_to_cm = 0.1;

static void url_receiver(char const* url, void* user_data)
{
    char* buffer = (char*)user_data;
    if (*buffer != '\0')
        return; // only keep first value

    if (strlen(url) < 256)
        strcpy(buffer, url);
}

static void head_pose_callback(tobii_head_pose_t const* head_pose, void* user_data)
{
    // Fold the pose into the supplied storage axis by axis: the value of an
    // invalid axis means nothing, so the storage keeps the last valid one.
    // The position is valid as a whole, each rotation axis on its own.
    tobii_head_pose_t* held_pose = (tobii_head_pose_t*)user_data;

    if (head_pose->position_validity == TOBII_VALIDITY_VALID)
    {
        held_pose->position_validity = TOBII_VALIDITY_VALID;
        for (int i = 0; i < 3; i++)
            held_pose->position_xyz[i] = head_pose->position_xyz[i];
    }

    for (int i = 0; i < 3; i++)
    {
        if (head_pose->rotation_validity_xyz[i] == TOBII_VALIDITY_VALID)
        {
            held_pose->rotation_validity_xyz[i] = TOBII_VALIDITY_VALID;
            held_pose->rotation_xyz[i] = head_pose->rotation_xyz[i];
        }
    }
}

tobii_tracker::tobii_tracker() = default;

tobii_tracker::~tobii_tracker()
{
    QMutexLocker lck(&mtx);
    if (device)
    {
        tobii_head_pose_unsubscribe(device);
        tobii_device_destroy(device);
    }
    if (api)
    {
        tobii_api_destroy(api);
    }
}

module_status tobii_tracker::start_tracker(QFrame*)
{
    QMutexLocker lck(&mtx);
    tobii_error_t tobii_error = tobii_api_create(&api, nullptr, nullptr);
    if (tobii_error != TOBII_ERROR_NO_ERROR)
    {
        return error("Failed to initialize the Tobii Stream Engine API.");
    }

    char url[256] = { 0 };
    tobii_error = tobii_enumerate_local_device_urls(api, url_receiver, url);
    if (tobii_error != TOBII_ERROR_NO_ERROR || url[0] == '\0')
    {
        tobii_api_destroy(api);
        api = nullptr;
        return error("No stream engine compatible device(s) found.");
    }

    // Stream Engine 4.0 added field_of_use; head tracking is an interactive
    // use. Older three-argument SDKs need this call reverted.
    tobii_error = tobii_device_create(api, url, TOBII_FIELD_OF_USE_INTERACTIVE, &device);
    if (tobii_error != TOBII_ERROR_NO_ERROR)
    {
        tobii_api_destroy(api);
        api = nullptr;
        return error(QString("Failed to connect to %1.").arg(url));
    }

    tobii_error = tobii_head_pose_subscribe(device, head_pose_callback, &held_head_pose);
    if (tobii_error != TOBII_ERROR_NO_ERROR)
    {
        tobii_device_destroy(device);
        device = nullptr;
        tobii_api_destroy(api);
        api = nullptr;
        return error("Failed to subscribe to head pose stream.");
    }

    return status_ok();
}

void tobii_tracker::data(double* data)
{
    QMutexLocker lck(&mtx);
    tobii_error_t tobii_error = tobii_device_process_callbacks(device);
    if (tobii_error == TOBII_ERROR_CONNECTION_FAILED)
    {
        // The connection to the tracker was lost (with libtobii.so, the
        // tobiid service restarted). Try to get it back at most once a
        // second, and keep reporting the last pose meanwhile.
        auto const now = std::chrono::steady_clock::now();
        if (now - last_reconnect >= std::chrono::seconds(1))
        {
            last_reconnect = now;
            tobii_device_reconnect(device);
        }
    }

    // opentrack hands in a zero pose every tick and subtracts the centre
    // from it, so an axis left out here would jump to minus the centre.
    // Fill in every axis that has had a valid value with its last one, also
    // when this tick brought an invalid pose, no pose or an error; leave out
    // only the axes that have had none yet.

    // Tobii coordinate system is different from opentrack's
    // Tobii: +x is to the right, +y is up, +z is towards the user
    // Rotation xyz is in radians, x is pitch, y is yaw, z is roll

    if (held_head_pose.position_validity == TOBII_VALIDITY_VALID)
    {
        data[TX] = -held_head_pose.position_xyz[0] * mm_to_cm;
        data[TY] = held_head_pose.position_xyz[1] * mm_to_cm;
        data[TZ] = held_head_pose.position_xyz[2] * mm_to_cm;
    }

    if (held_head_pose.rotation_validity_xyz[0] == TOBII_VALIDITY_VALID)
    {
        data[Pitch] = held_head_pose.rotation_xyz[0] * rad_to_deg;
    }

    if (held_head_pose.rotation_validity_xyz[1] == TOBII_VALIDITY_VALID)
    {
        data[Yaw] = -held_head_pose.rotation_xyz[1] * rad_to_deg;
    }

    if (held_head_pose.rotation_validity_xyz[2] == TOBII_VALIDITY_VALID)
    {
        data[Roll] = held_head_pose.rotation_xyz[2] * rad_to_deg;
    }
}

OPENTRACK_DECLARE_TRACKER(tobii_tracker, tobii_dialog, tobii_metadata)
