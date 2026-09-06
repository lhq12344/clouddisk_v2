#include "../application/account/AccountPorts.h"
#include "../application/account/EmailVerificationCommandService.h"
#include "../application/account/RegistrationService.h"
#include "../application/account/SigninService.h"
#include "../application/account/UserInfoService.h"
#include "../application/common/FeatureFlags.h"
#include "../application/common/OutboxPort.h"
#include "../application/file/FileDeleteService.h"
#include "../application/file/FileReadService.h"
#include "../application/upload/MultipartUploadService.h"
#include "../application/upload/UploadStateMachine.h"
#include "../domain/common/RequestContext.h"
#include "../domain/common/Result.h"

#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>

using namespace core;

namespace
{
void setEnv(const char *key, const char *value)
{
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}

void unsetEnv(const char *key)
{
#ifdef _WIN32
    _putenv_s(key, "");
#else
    unsetenv(key);
#endif
}
} // namespace

class ProbeAccountReadPort final : public application::account::AccountReadPort
{
public:
    bool emailExists{false};
    bool usernameExists{false};

    void getUserInfo(const domain::RequestContext &, UserInfoCallback callback) override
    {
        application::account::UserInfo user;
        user.id = 7;
        user.username = "probe";
        callback(domain::Result<application::account::UserInfo>::ok(std::move(user)));
    }

    void findCredentialsByUsername(const std::string &username, CredentialsCallback callback) override
    {
        if (username != "probe")
        {
            callback(domain::Result<application::account::AccountCredentials>::fail(domain::ErrorCode::NotFound, "not found"));
            return;
        }
        application::account::AccountCredentials credentials;
        credentials.id = 7;
        credentials.username = username;
        credentials.salt = "salt";
        credentials.passwordHash = "hash";
        callback(domain::Result<application::account::AccountCredentials>::ok(std::move(credentials)));
    }

    void accountExistsByEmail(const std::string &, ExistsCallback callback) override
    {
        callback(domain::Result<bool>::ok(emailExists));
    }

    void accountExistsByUsername(const std::string &, ExistsCallback callback) override
    {
        callback(domain::Result<bool>::ok(usernameExists));
    }
};

class ProbeAccountWritePort final : public application::account::AccountWritePort
{
public:
    int createCalls{0};

    void createAccountFromPendingRegistration(const application::account::PendingRegistration &,
                                              CreateAccountCallback callback) override
    {
        createCalls++;
        callback(domain::Result<void>::success());
    }
};

class ProbePendingRegistrationPort final : public application::account::PendingRegistrationPort
{
public:
    int storeCalls{0};
    int deleteCalls{0};
    bool pendingMissing{false};
    std::string code{"123456"};
    application::account::PendingRegistration pending{"probe", "probe@example.com", "hash", "salt"};

    void storePendingRegistration(const application::account::PendingRegistration &value,
                                  StoreCallback callback) override
    {
        storeCalls++;
        pending = value;
        callback(domain::Result<void>::success());
    }

    void loadPendingRegistration(const std::string &, LoadCallback callback) override
    {
        if (pendingMissing)
        {
            callback(domain::Result<application::account::PendingRegistration>::fail(domain::ErrorCode::NotFound, "pending registration not found"));
            return;
        }
        callback(domain::Result<application::account::PendingRegistration>::ok(pending));
    }

    void loadVerificationCode(const std::string &, CodeCallback callback) override
    {
        callback(domain::Result<std::string>::ok(code));
    }

    void deletePendingRegistrationAndCode(const std::string &, StoreCallback callback) override
    {
        deleteCalls++;
        callback(domain::Result<void>::success());
    }
};

class ProbePasswordHasher final : public application::account::PasswordHasher
{
public:
    domain::Result<application::account::PendingRegistration> hashPendingRegistration(const std::string &username,
                                                                                      const std::string &email,
                                                                                      const std::string &) const override
    {
        application::account::PendingRegistration pending;
        pending.username = username;
        pending.email = email;
        pending.passwordHash = "hash";
        pending.salt = "salt";
        return domain::Result<application::account::PendingRegistration>::ok(std::move(pending));
    }
};

class ProbePasswordVerifier final : public application::account::PasswordVerifier
{
public:
    bool verifyLegacyPassword(const std::string &rawPassword,
                              const std::string &,
                              const std::string &) const override
    {
        return rawPassword == "secret";
    }
};

class ProbeJwtIssuer final : public application::account::JwtIssuer
{
public:
    domain::Result<std::string> issueSigninToken(std::uint64_t userId, const std::string &username) const override
    {
        return domain::Result<std::string>::ok("token:" + std::to_string(userId) + ":" + username);
    }
};

class ProbeFileReadPort final : public application::file::FileReadPort
{
public:
    int listCalls{0};

    void listOwnedFiles(const domain::RequestContext &ctx, ListCallback callback) override
    {
        listCalls++;
        application::file::FileListItem file;
        file.ownerId = ctx.userId.value_or(0);
        file.filename = "probe.txt";
        file.filehash = "abc";
        file.status = "success";
        file.objectKey = "files/abc";
        callback(domain::Result<std::vector<application::file::FileListItem>>::ok({file}));
    }

    void resolveOwnedFile(const domain::RequestContext &ctx,
                          const application::file::FileAccessRequest &,
                          ResolveCallback callback) override
    {
        application::file::FileListItem file;
        file.ownerId = ctx.userId.value_or(0);
        file.filename = "probe.txt";
        file.filehash = "abc";
        file.status = status;
        file.objectKey = objectKey;
        callback(domain::Result<application::file::FileListItem>::ok(std::move(file)));
    }

    std::string status{"success"};
    std::string objectKey{"files/abc"};
};

class ProbeFileStorageControlPort final : public application::file::StorageControlPort
{
public:
    int presignCalls{0};

    void presignGet(const domain::RequestContext &,
                    const application::file::FileListItem &,
                    const application::file::PresignedGetRequest &request,
                    PresignCallback callback) override
    {
        presignCalls++;
        if (request.objectKey != "files/abc")
        {
            callback(domain::Result<std::string>::fail(domain::ErrorCode::InvalidArgument, "bad object key"));
            return;
        }
        callback(domain::Result<std::string>::ok("https://storage.example/files/abc"));
    }
};

class ProbeFileMutationPort final : public application::file::FileMutationPort
{
public:
    int deleteCalls{0};

    void deleteOwnedFile(const domain::RequestContext &,
                         const application::file::DeleteFileCommand &command,
                         DeleteCallback callback) override
    {
        deleteCalls++;
        application::file::DeleteFileResult result;
        result.fileId = 42;
        result.objectKey = "files/abc";
        result.objectDeleteQueued = command.filehash == "last-ref";
        callback(domain::Result<application::file::DeleteFileResult>::ok(std::move(result)));
    }
};

class ProbeOutboxPort final : public application::OutboxPort
{
public:
    int enqueueCalls{0};
    application::OutboxMessage lastMessage;

    void enqueue(const application::OutboxMessage &message, EnqueueCallback callback) override
    {
        enqueueCalls++;
        lastMessage = message;
        callback(domain::Result<void>::success());
    }
};

class ProbeUploadPorts final : public application::upload::UploadSessionPort,
                               public application::upload::MultipartStorageControlPort,
                               public application::upload::UploadFinalizationPort
{
public:
    int completeCalls{0};
    application::upload::UploadSession session;

    ProbeUploadPorts()
    {
        session.uploadId = "upload-1";
        session.ownerAccountId = 7;
        session.fileName = "probe.bin";
        session.fileHash = "abc";
        session.fileSize = 8;
        session.objectKey = "files/abc";
        session.storageUploadId = "storage-upload-1";
        session.partSize = 4;
        session.totalParts = 2;
        session.state = application::upload::UploadState::Uploading;
    }

    void saveInitiatedSession(const application::upload::UploadSession &value, SaveCallback callback) override
    {
        session = value;
        callback(domain::Result<void>::success());
    }

    void loadOwnedSession(const domain::RequestContext &ctx,
                          const std::string &uploadId,
                          LoadCallback callback) override
    {
        if (uploadId != session.uploadId || ctx.userId.value_or(0) != session.ownerAccountId)
        {
            callback(domain::Result<application::upload::UploadSession>::fail(domain::ErrorCode::NotFound, "upload session not found"));
            return;
        }
        callback(domain::Result<application::upload::UploadSession>::ok(session));
    }

    void transitionState(const std::string &uploadId,
                         application::upload::UploadState expected,
                         application::upload::UploadState next,
                         StateCallback callback) override
    {
        if (uploadId != session.uploadId || session.state != expected)
        {
            callback(domain::Result<void>::fail(domain::ErrorCode::Conflict, "upload session state changed"));
            return;
        }
        session.state = next;
        callback(domain::Result<void>::success());
    }

    void initiateMultipart(const domain::RequestContext &ctx,
                           const application::upload::InitUploadCommand &command,
                           InitCallback callback) override
    {
        session.ownerAccountId = ctx.userId.value_or(0);
        session.fileName = command.fileName;
        session.fileHash = command.fileHash;
        session.fileSize = command.fileSize;
        session.objectKey = "files/" + command.fileHash;
        callback(domain::Result<application::upload::UploadSession>::ok(session));
    }

    void presignParts(const domain::RequestContext &,
                      const application::upload::UploadSession &,
                      const std::vector<int> &partNumbers,
                      PresignCallback callback) override
    {
        application::upload::PresignPartsResult result;
        result.uploadId = session.uploadId;
        for (const auto partNumber : partNumbers)
        {
            result.parts.push_back({partNumber, "https://storage.example/part", "soon"});
        }
        callback(domain::Result<application::upload::PresignPartsResult>::ok(std::move(result)));
    }

    void status(const domain::RequestContext &, const application::upload::UploadSession &, StatusCallback callback) override
    {
        application::upload::UploadStatusResult result;
        result.totalParts = session.totalParts;
        result.uploadedParts = {1, 2};
        callback(domain::Result<application::upload::UploadStatusResult>::ok(std::move(result)));
    }

    void listParts(const domain::RequestContext &, const application::upload::UploadSession &, ListPartsCallback callback) override
    {
        callback(domain::Result<std::vector<application::upload::UploadedPart>>::ok({{1, "etag-1"}, {2, "etag-2"}}));
    }

    void completeMultipart(const domain::RequestContext &,
                           const application::upload::UploadSession &,
                           const std::vector<application::upload::UploadedPart> &,
                           CompleteCallback callback) override
    {
        completeCalls++;
        application::upload::CompleteUploadResult result;
        result.uploadId = session.uploadId;
        result.objectKey = session.objectKey;
        result.etag = "etag-final";
        result.status = "pending_scan";
        callback(domain::Result<application::upload::CompleteUploadResult>::ok(std::move(result)));
    }

    void abort(const domain::RequestContext &, const application::upload::UploadSession &, AbortCallback callback) override
    {
        session.state = application::upload::UploadState::Aborted;
        callback(domain::Result<void>::success());
    }

    void finalizeCompletedUpload(const domain::RequestContext &,
                                 const application::upload::UploadSession &,
                                 const std::string &objectEtag,
                                 FinalizeCallback callback) override
    {
        session.state = application::upload::UploadState::PendingScan;
        application::upload::CompleteUploadResult result;
        result.uploadId = session.uploadId;
        result.objectKey = session.objectKey;
        result.etag = objectEtag;
        result.status = application::upload::toString(session.state);
        callback(domain::Result<application::upload::CompleteUploadResult>::ok(std::move(result)));
    }
};

static_assert(application::upload::canTransition(
    application::upload::UploadState::Initiated,
    application::upload::UploadState::Completing));
static_assert(application::upload::canTransition(
    application::upload::UploadState::Completing,
    application::upload::UploadState::PendingScan));
static_assert(!application::upload::canTransition(
    application::upload::UploadState::Ready,
    application::upload::UploadState::PendingScan));

static_assert(std::is_default_constructible_v<application::CoreFeatureFlags>);
static_assert(std::is_default_constructible_v<domain::RequestContext>);
static_assert(std::is_default_constructible_v<application::account::PendingRegistration>);
static_assert(std::is_default_constructible_v<application::file::FileAccessRequest>);
static_assert(std::is_default_constructible_v<application::upload::UploadSession>);

int main()
{
    domain::RequestContext requestContext;
    requestContext.requestId = "compile-probe";
    requestContext.userId = 7;
    requestContext.username = "probe";

    application::file::FileAuthorizationPolicy policy;
    if (!policy.canReadObject(requestContext, 7, "success"))
    {
        return 1;
    }
    if (policy.canReadObject(requestContext, 8, "success"))
    {
        return 2;
    }
    if (policy.canReadObject(requestContext, 7, "pending_scan"))
    {
        return 3;
    }
    if (application::parseImplementationRoute("legacy") != application::ImplementationRoute::Legacy)
    {
        return 4;
    }
    if (application::parseImplementationRoute("compare") != application::ImplementationRoute::Shadow)
    {
        return 5;
    }
    if (application::parseImplementationRoute("unexpected", application::ImplementationRoute::Core) != application::ImplementationRoute::Core)
    {
        return 6;
    }
    if (application::toString(application::ImplementationRoute::Core) != "core")
    {
        return 7;
    }
    setEnv("CORE_FILE_READS", "shadow");
    setEnv("CORE_FILE_DELETE", "legacy");
    const auto flags = application::CoreFeatureFlags::fromEnvironment();
    unsetEnv("CORE_FILE_READS");
    unsetEnv("CORE_FILE_DELETE");
    if (flags.fileReads != application::ImplementationRoute::Shadow)
    {
        return 8;
    }
    if (flags.fileDelete != application::ImplementationRoute::Legacy)
    {
        return 9;
    }
    if (flags.accountReads != application::ImplementationRoute::Core)
    {
        return 10;
    }

    ProbeAccountReadPort accounts;
    ProbePasswordVerifier passwordVerifier;
    ProbeJwtIssuer jwtIssuer;
    application::account::SigninService signin(accounts, passwordVerifier, jwtIssuer, application::CoreFeatureFlags{});
    std::string issuedToken;
    signin.signin({"probe", "secret"}, [&issuedToken](auto result) {
        if (result.ok())
        {
            issuedToken = result.value;
        }
    });
    if (issuedToken != "token:7:probe")
    {
        return 11;
    }

    ProbeAccountWritePort accountWriter;
    ProbePendingRegistrationPort pendingRegistrations;
    ProbePasswordHasher passwordHasher;
    application::account::RegistrationService registration(accounts, accountWriter, pendingRegistrations, passwordHasher, application::CoreFeatureFlags{});
    bool signupOk = false;
    registration.signup({"probe", "secret", "probe@example.com"}, [&signupOk](auto result) {
        signupOk = result.ok();
    });
    if (!signupOk || pendingRegistrations.storeCalls != 1)
    {
        return 16;
    }
    bool verifyOk = false;
    registration.verifyCode({"probe@example.com", "123456"}, [&verifyOk](auto result) {
        verifyOk = result.ok();
    });
    if (!verifyOk || accountWriter.createCalls != 1 || pendingRegistrations.deleteCalls != 1)
    {
        return 17;
    }
    pendingRegistrations.pendingMissing = true;
    accounts.emailExists = true;
    verifyOk = false;
    registration.verifyCode({"probe@example.com", "123456"}, [&verifyOk](auto result) {
        verifyOk = result.ok();
    });
    if (!verifyOk || accountWriter.createCalls != 1)
    {
        return 18;
    }

    ProbeOutboxPort outbox;
    application::account::EmailVerificationCommandService emailCommands(outbox, application::CoreFeatureFlags{});
    bool emailQueued = false;
    emailCommands.sendVerificationCode({"probe@example.com", "rid-1"}, [&emailQueued](auto result) {
        emailQueued = result.ok();
    });
    if (!emailQueued || outbox.enqueueCalls != 1 || outbox.lastMessage.eventType != "EMAIL_VERIFICATION_REQUESTED" || outbox.lastMessage.topic != "email_verify")
    {
        return 19;
    }

    ProbeFileReadPort files;
    ProbeFileStorageControlPort fileStorage;
    application::file::FileReadService fileReads(files, fileStorage, application::file::FileAuthorizationPolicy{}, application::CoreFeatureFlags{});
    std::string downloadUrl;
    fileReads.presignDownload(requestContext, {"probe.txt", "abc", 8}, false, [&downloadUrl](auto result) {
        if (result.ok())
        {
            downloadUrl = result.value;
        }
    });
    if (downloadUrl != "https://storage.example/files/abc" || fileStorage.presignCalls != 1)
    {
        return 12;
    }
    files.status = "pending_scan";
    downloadUrl.clear();
    fileReads.presignDownload(requestContext, {"probe.txt", "abc", 8}, false, [&downloadUrl](auto result) {
        if (result.ok())
        {
            downloadUrl = result.value;
        }
    });
    if (!downloadUrl.empty() || fileStorage.presignCalls != 1)
    {
        return 13;
    }

    ProbeFileMutationPort fileMutations;
    application::file::FileDeleteService fileDelete(fileMutations, application::CoreFeatureFlags{});
    bool deleteOk = false;
    bool deleteQueued = false;
    fileDelete.deleteFile(requestContext, {"probe.txt", "last-ref"}, [&deleteOk, &deleteQueued](auto result) {
        deleteOk = result.ok();
        if (result.ok())
        {
            deleteQueued = result.value.objectDeleteQueued;
        }
    });
    if (!deleteOk || !deleteQueued || fileMutations.deleteCalls != 1)
    {
        return 20;
    }

    ProbeUploadPorts uploadPorts;
    application::upload::MultipartUploadService uploads(uploadPorts, uploadPorts, uploadPorts, application::CoreFeatureFlags{});
    std::string completeStatus;
    uploads.complete(requestContext, "upload-1", [&completeStatus](auto result) {
        if (result.ok())
        {
            completeStatus = result.value.status;
        }
    });
    if (completeStatus != "pending_scan" || uploadPorts.completeCalls != 1)
    {
        return 14;
    }
    uploads.complete(requestContext, "upload-1", [&completeStatus](auto result) {
        if (result.ok())
        {
            completeStatus = result.value.status;
        }
    });
    if (completeStatus != "pending_scan" || uploadPorts.completeCalls != 1)
    {
        return 15;
    }

    return 0;
}
