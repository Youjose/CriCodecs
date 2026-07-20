#pragma once
/**
 * @file cricodecs.hpp
 * @brief Umbrella header for the supported CriCodecs C++ API.
 *
 * Applications may include this file for the complete public surface or include
 * individual format headers from <cricodecs/...> to reduce compile time.
 */

#include <cricodecs/version.hpp>

#include "aax/aax_container.hpp"
#include "acb/acb_commands.hpp"
#include "acb/acb_container.hpp"
#include "acx/acx_builder.hpp"
#include "acx/acx_container.hpp"
#include "adx/adx_codec.hpp"
#include "adx/adx_key_recovery.hpp"
#include "afs/afs_container.hpp"
#include "ahx/ahx_codec.hpp"
#include "ahx/ahx_key_recovery.hpp"
#include "aix/aix_container.hpp"
#include "awb/awb_aac_encryption.hpp"
#include "awb/awb_aac_key_recovery.hpp"
#include "awb/awb_container.hpp"
#include "cpk/cpk_container.hpp"
#include "csb/csb_container.hpp"
#include "cvm/cvm_build_script.hpp"
#include "cvm/cvm_builder.hpp"
#include "cvm/cvm_container.hpp"
#include "cvm/cvm_volume_set.hpp"
#include "hca/hca_codec.hpp"
#include "hca/hca_key_recovery.hpp"
#include "key_recovery/key_recovery.hpp"
#include "sfd/sfd_container.hpp"
#include "usm/usm_container.hpp"
#include "usm/usm_key_recovery.hpp"
#include "utf/utf_table.hpp"
#include "video/h264.hpp"
#include "video/ivf.hpp"
#include "video/mpeg.hpp"
#include "wav/wav_container.hpp"
