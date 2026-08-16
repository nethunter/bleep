#pragma once

// Public verification keys only. Matching private keys belong in protected
// GitHub Environments and must never be committed to this repository.
namespace firmware_update_keys {

constexpr const char kStableKeyId[] = "stable-2026-01";
constexpr const char kStablePublicKey[] = R"KEY(-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEo/22JbP44qxFlaayF2CefHlFcPuh
yGlg2NGQAQu5m954i3rIoVS9JTsm6ZzxP/rbfwP78D8WCnqDcATXTVPiNg==
-----END PUBLIC KEY-----
)KEY";

constexpr const char kDevelopmentKeyId[] = "development-2026-01";
constexpr const char kDevelopmentPublicKey[] = R"KEY(-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEHKGZb9U6YxiTdb/hh1ljMzyjYrfP
Vc5ckH39OYQl87u/jyyhbT/BkjR0+/FKqSSkM8uiVVrdfZkO1ubAhzvm2w==
-----END PUBLIC KEY-----
)KEY";

}  // namespace firmware_update_keys
