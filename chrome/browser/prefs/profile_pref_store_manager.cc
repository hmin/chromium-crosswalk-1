// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prefs/profile_pref_store_manager.h"

#include "base/file_util.h"
#include "base/json/json_file_value_serializer.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/prefs/json_pref_store.h"
#include "base/prefs/persistent_pref_store.h"
#include "base/prefs/pref_registry_simple.h"
#include "chrome/browser/prefs/pref_hash_store_impl.h"
#include "chrome/browser/prefs/tracked/pref_service_hash_store_contents.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
#include "components/user_prefs/pref_registry_syncable.h"

namespace {

// An in-memory PrefStore backed by an immutable DictionaryValue.
class DictionaryPrefStore : public PrefStore {
 public:
  explicit DictionaryPrefStore(const base::DictionaryValue* dictionary)
      : dictionary_(dictionary) {}

  virtual bool GetValue(const std::string& key,
                        const base::Value** result) const OVERRIDE {
    const base::Value* tmp = NULL;
    if (!dictionary_->Get(key, &tmp))
      return false;

    if (result)
      *result = tmp;
    return true;
  }

 private:
  virtual ~DictionaryPrefStore() {}

  const base::DictionaryValue* dictionary_;

  DISALLOW_COPY_AND_ASSIGN(DictionaryPrefStore);
};

}  // namespace

// TODO(erikwright): Enable this on Chrome OS and Android once MACs are moved
// out of Local State. This will resolve a race condition on Android and a
// privacy issue on ChromeOS. http://crbug.com/349158
const bool ProfilePrefStoreManager::kPlatformSupportsPreferenceTracking =
#if defined(OS_ANDROID) || defined(OS_CHROMEOS)
    false;
#else
    true;
#endif

// Waits for a PrefStore to be initialized and then initializes the
// corresponding PrefHashStore.
// The observer deletes itself when its work is completed.
class ProfilePrefStoreManager::InitializeHashStoreObserver
    : public PrefStore::Observer {
 public:
  // Creates an observer that will initialize |pref_hash_store| with the
  // contents of |pref_store| when the latter is fully loaded.
  InitializeHashStoreObserver(
      const std::vector<PrefHashFilter::TrackedPreferenceMetadata>&
          tracking_configuration,
      size_t reporting_ids_count,
      const scoped_refptr<PrefStore>& pref_store,
      scoped_ptr<PrefHashStoreImpl> pref_hash_store_impl)
      : tracking_configuration_(tracking_configuration),
        reporting_ids_count_(reporting_ids_count),
        pref_store_(pref_store),
        pref_hash_store_impl_(pref_hash_store_impl.Pass()) {}

  virtual ~InitializeHashStoreObserver();

  // PrefStore::Observer implementation.
  virtual void OnPrefValueChanged(const std::string& key) OVERRIDE;
  virtual void OnInitializationCompleted(bool succeeded) OVERRIDE;

 private:
  const std::vector<PrefHashFilter::TrackedPreferenceMetadata>
      tracking_configuration_;
  const size_t reporting_ids_count_;
  scoped_refptr<PrefStore> pref_store_;
  scoped_ptr<PrefHashStoreImpl> pref_hash_store_impl_;

  DISALLOW_COPY_AND_ASSIGN(InitializeHashStoreObserver);
};

ProfilePrefStoreManager::InitializeHashStoreObserver::
    ~InitializeHashStoreObserver() {}

void ProfilePrefStoreManager::InitializeHashStoreObserver::OnPrefValueChanged(
    const std::string& key) {}

void
ProfilePrefStoreManager::InitializeHashStoreObserver::OnInitializationCompleted(
    bool succeeded) {
  // If we successfully loaded the preferences _and_ the PrefHashStoreImpl
  // hasn't been initialized by someone else in the meantime, initialize it now.
  const PrefHashStoreImpl::StoreVersion pre_update_version =
      pref_hash_store_impl_->GetCurrentVersion();
  if (succeeded && pre_update_version < PrefHashStoreImpl::VERSION_LATEST) {
    PrefHashFilter(pref_hash_store_impl_.PassAs<PrefHashStore>(),
                   tracking_configuration_,
                   reporting_ids_count_).Initialize(*pref_store_);
    UMA_HISTOGRAM_ENUMERATION(
        "Settings.TrackedPreferencesAlternateStoreVersionUpdatedFrom",
        pre_update_version,
        PrefHashStoreImpl::VERSION_LATEST + 1);
  }
  pref_store_->RemoveObserver(this);
  delete this;
}

ProfilePrefStoreManager::ProfilePrefStoreManager(
    const base::FilePath& profile_path,
    const std::vector<PrefHashFilter::TrackedPreferenceMetadata>&
        tracking_configuration,
    size_t reporting_ids_count,
    const std::string& seed,
    const std::string& device_id,
    PrefService* local_state)
    : profile_path_(profile_path),
      tracking_configuration_(tracking_configuration),
      reporting_ids_count_(reporting_ids_count),
      seed_(seed),
      device_id_(device_id),
      local_state_(local_state) {}

ProfilePrefStoreManager::~ProfilePrefStoreManager() {}

// static
void ProfilePrefStoreManager::RegisterPrefs(PrefRegistrySimple* registry) {
  PrefServiceHashStoreContents::RegisterPrefs(registry);
}

// static
void ProfilePrefStoreManager::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  PrefHashFilter::RegisterProfilePrefs(registry);
}

// static
base::FilePath ProfilePrefStoreManager::GetPrefFilePathFromProfilePath(
    const base::FilePath& profile_path) {
  return profile_path.Append(chrome::kPreferencesFilename);
}

// static
void ProfilePrefStoreManager::ResetAllPrefHashStores(PrefService* local_state) {
  PrefServiceHashStoreContents::ResetAllPrefHashStores(local_state);
}

//  static
base::Time ProfilePrefStoreManager::GetResetTime(PrefService* pref_service) {
  return PrefHashFilter::GetResetTime(pref_service);
}

// static
void ProfilePrefStoreManager::ClearResetTime(PrefService* pref_service) {
  PrefHashFilter::ClearResetTime(pref_service);
}

void ProfilePrefStoreManager::ResetPrefHashStore() {
  if (kPlatformSupportsPreferenceTracking)
    GetPrefHashStoreImpl()->Reset();
}

PersistentPrefStore* ProfilePrefStoreManager::CreateProfilePrefStore(
    const scoped_refptr<base::SequencedTaskRunner>& io_task_runner) {
  scoped_ptr<PrefFilter> pref_filter;
  if (kPlatformSupportsPreferenceTracking) {
    pref_filter.reset(
        new PrefHashFilter(GetPrefHashStoreImpl().PassAs<PrefHashStore>(),
                           tracking_configuration_,
                           reporting_ids_count_));
  }
  return new JsonPrefStore(GetPrefFilePathFromProfilePath(profile_path_),
                           io_task_runner,
                           pref_filter.Pass());
}

void ProfilePrefStoreManager::UpdateProfileHashStoreIfRequired(
    const scoped_refptr<base::SequencedTaskRunner>& io_task_runner) {
  if (!kPlatformSupportsPreferenceTracking)
    return;
  scoped_ptr<PrefHashStoreImpl> pref_hash_store_impl(GetPrefHashStoreImpl());
  const PrefHashStoreImpl::StoreVersion current_version =
      pref_hash_store_impl->GetCurrentVersion();
  UMA_HISTOGRAM_ENUMERATION("Settings.TrackedPreferencesAlternateStoreVersion",
                            current_version,
                            PrefHashStoreImpl::VERSION_LATEST + 1);

  // Update the pref hash store if it's not at the latest version.
  if (current_version != PrefHashStoreImpl::VERSION_LATEST) {
    scoped_refptr<JsonPrefStore> pref_store =
        new JsonPrefStore(GetPrefFilePathFromProfilePath(profile_path_),
                          io_task_runner,
                          scoped_ptr<PrefFilter>());
    pref_store->AddObserver(
        new InitializeHashStoreObserver(tracking_configuration_,
                                        reporting_ids_count_,
                                        pref_store,
                                        pref_hash_store_impl.Pass()));
    pref_store->ReadPrefsAsync(NULL);
  }
}

bool ProfilePrefStoreManager::InitializePrefsFromMasterPrefs(
    const base::DictionaryValue& master_prefs) {
  // Create the profile directory if it doesn't exist yet (very possible on
  // first run).
  if (!base::CreateDirectory(profile_path_))
    return false;

  JSONFileValueSerializer serializer(
      GetPrefFilePathFromProfilePath(profile_path_));

  // Call Serialize (which does IO) on the main thread, which would _normally_
  // be verboten. In this case however, we require this IO to synchronously
  // complete before Chrome can start (as master preferences seed the Local
  // State and Preferences files). This won't trip ThreadIORestrictions as they
  // won't have kicked in yet on the main thread.
  bool success = serializer.Serialize(master_prefs);

  if (success && kPlatformSupportsPreferenceTracking) {
    scoped_refptr<const PrefStore> pref_store(
        new DictionaryPrefStore(&master_prefs));
    PrefHashFilter(GetPrefHashStoreImpl().PassAs<PrefHashStore>(),
                   tracking_configuration_,
                   reporting_ids_count_).Initialize(*pref_store);
  }

  UMA_HISTOGRAM_BOOLEAN("Settings.InitializedFromMasterPrefs", success);
  return success;
}

scoped_ptr<PrefHashStoreImpl> ProfilePrefStoreManager::GetPrefHashStoreImpl() {
  DCHECK(kPlatformSupportsPreferenceTracking);

  return make_scoped_ptr(new PrefHashStoreImpl(
      seed_,
      device_id_,
      scoped_ptr<HashStoreContents>(new PrefServiceHashStoreContents(
          profile_path_.AsUTF8Unsafe(), local_state_))));
}
