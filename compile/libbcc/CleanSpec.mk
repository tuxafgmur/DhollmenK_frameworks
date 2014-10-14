# Copyright (C) 2007 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# ************************************************
# NEWER CLEAN STEPS MUST BE AT THE END OF THE LIST
# ************************************************

$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/SHARED_LIBRARIES/libbcc_intermediates)
$(call add-clean-step, rm -rf $(HOST_OUT)/obj/SHARED_LIBRARIES/libbcc_intermediates)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/lib/libruntime.bc)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/SHARED_LIBRARIES/libclcore.bc_intermediates)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/SHARED_LIBRARIES/libbcc_sha1_intermediates)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/lib/libbcc_sha1.so)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/SHARED_LIBRARIES/libclcore_neon.bc_intermediates)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/SHARED_LIBRARIES/libclcore*.bc_intermediates)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/SHARED_LIBRARIES/libbcinfo_intermediates)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/SHARED_LIBRARIES/libbc*_intermediates)
$(call add-clean-step, rm -rf $(HOST_OUT)/obj/STATIC_LIBRARIES/libbc*_intermediates)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/STATIC_LIBRARIES/libbc*_intermediates)
$(call add-clean-step, rm -rf $(HOST_OUT)/obj/SHARED_LIBRARIES/libbc*_intermediates)
