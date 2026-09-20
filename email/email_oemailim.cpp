#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include "email_handler_c.h"
#include "email_handler.h"
#include "x_mailer.h"
#include "email_opt_163_impl.h"
#include "email_opt_outlook_impl.h"
#include "email_opt_gmail_impl.h"
#include "db_connection.h"
#include "email_repo.h"
#include "session_repo.h"
#include "key_repo.h"
#include "task_repo.h"
#include "file_transfer_repo.h"
#include "signal_session_repo.h"
#include "persistence/group_session_repo.h"
#include "unified/unified_session_repo.h"
#include "unified/unified_session_manager.h"
#include "file_chunk_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <map>
#include <vector>
#include <random>
#include <chrono>

#include <vmime/vmime.hpp>
#include <vmime/platforms/posix/posixHandler.hpp>
#include <vmime/contentDispositionField.hpp>
#include <vmime/contentTypeField.hpp>

using json = nlohmann::json;

// Forward declaration for extractParts (defined later in this file)
static void extractParts(const vmime::shared_ptr<vmime::bodyPart>& part,
                         std::string& textBody, std::string& htmlBody,
                         nlohmann::json& attachments, bool& hasAttachment);

// Forward declaration for email_add_email_to_session from email_session.cpp
extern "C" int email_add_email_to_session(const char* sessionId, const char* uuid, const char* account, int encrypt_method, char* outJson, int outSize);

// Forward declaration for email_create_session from email_session.cpp
extern "C" int email_create_session(const char* account, const char* subject, const char* members, const char* message_id, int encrypt_method, int64_t localemail_rowid, char* outJson, int outSize);

int oemailim_system_open(const char* dataDir, const char* configDir, const char* logDir) {
    return systemOpen_c(dataDir, configDir, logDir);
}

void oemailim_set_callback(void* callback) {
}

extern "C" int oemailim_open_new_email(const char* email_id) {
    return OpenNewEmail_c(email_id);
}

int oemailim_go(int configIndex) {
    return Go_c(configIndex);
}

extern "C" int oemailim_authority(int configIndex) {
    LOG_INFO("oemailim_authority called with configIndex: %d", configIndex);
    int result = Authority_c(configIndex);
    LOG_INFO("oemailim_authority result: %d", result);
    return result;
}

extern "C" int oemailim_add_outlook_email() {
    LOG_INFO("oemailim_add_outlook_email called");
    return AddOutlookEmail_c();
}

extern "C" int oemailim_set_imap_server(int configIndex, const char* server, int port) {
    return SetImapServer_c(configIndex, server, port);
}

extern "C" int oemailim_set_smtp_server(int configIndex, const char* server, int port) {
    return SetSmtpServer_c(configIndex, server, port);
}

extern "C" int oemailim_set_refresh_token(int configIndex, const char* token) {
    return SetRefreshToken_c(configIndex, token);
}

extern "C" int oemailim_refresh_token(int configIndex) {
    return RefreshToken_c(configIndex);
}

extern "C" int oemailim_get_email(int configIndex, char* outEmail, int outSize) {
    return GetEmailAddress_c(configIndex, outEmail, outSize);
}

extern "C" int oemailim_get_refresh_token(int configIndex, char* outToken, int outSize) {
    return GetRefreshToken_c(configIndex, outToken, outSize);
}

extern "C" void oemailim_system_close(int configIndex) {
    systemClose_c(configIndex);
}

extern "C" int oemailim_email_list(int configIndex, const char* path, char* outJson, int outSize) {
    return Email_List_c(configIndex, path, outJson, outSize);
}

extern "C" int oemailim_email_select(int configIndex, const char* path, char* outJson, int outSize) {
    return Email_Select_c(configIndex, path, outJson, outSize);
}

extern "C" int email_idle_wait(int configIndex, const char* folder, int timeoutSeconds) {
    return IdleWait_c(configIndex, folder, timeoutSeconds);
}

extern "C" int email_find_sent_folder(int configIndex, char* outFolder, int outSize) {
    return FindSentFolder_c(configIndex, outFolder, outSize);
}

extern "C" int email_send_via_config(int configIndex, const char* content) {
    return SendEmail_c(configIndex, content);
}

extern "C" int email_count_pending_bodies(const char* account) {
    if (!g_db || !account) {
        LOG_INFO("[DB] count_pending_bodies: db or account is null\n");
        return -1;
    }

    LOG_INFO("[DB] count_pending_bodies: checking for account=%s\n", account);

    static EmailRepo s_emailRepo;
    int count = s_emailRepo.countPendingBodies(account);

    LOG_INFO("[DB] count_pending_bodies: found %d pending emails for account=%s\n", count, account);
    return count;
}

extern "C" int email_get_last_error(int configIndex, char* outBuf, int outSize) {
    return GetLastError_c(configIndex, outBuf, outSize);
}

extern "C" int email_download_pending_bodies(int configIndex, const char* account,
                                              const char* storageDir, char* outJson, int outSize) {
    if (!g_db) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        }
        return -1;
    }

    if (!account || !storageDir) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"null_parameter"})");
        }
        return -2;
    }

    std::string accountStr(account);
    std::string storageDirStr(storageDir);

    static EmailRepo s_emailRepo;
    static SessionRepo s_sessionRepo;
    static KeyRepo s_keyRepo;
    static TaskRepo s_taskRepo;
    static GroupSessionRepo s_groupRepo;

    auto pendingRecs = s_emailRepo.queryPendingEmails(accountStr, 10);

    struct PendingEmail {
        std::string uuid;
        std::string folder;
        int islocal = 0;
    };
    std::vector<PendingEmail> pending;
    for (const auto& pr : pendingRecs) {
        pending.push_back({pr.uuid, pr.folder, pr.islocal});
    }

    if (pending.empty()) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"success","downloaded":0})");
        }
        return 0;
    }

    LOG_INFO("[DB] download_pending: found %zu emails to download for %s\n", pending.size(), accountStr.c_str());

    std::string accountDir = storageDirStr + "/" + accountStr;
    std::filesystem::create_directories(accountDir);

    int downloaded = 0;
    json results = json::array();

    // Phase 1: Download all pending EMLs and classify by x_session_chart
    struct DownloadedEml {
        std::string uuid;
        std::string folder;
        std::string filePath;
        std::string emlContent;
        std::string message_id;
        std::string in_reply_to;
        std::string x_session_chart;
        std::string eml_subject;
        std::string eml_to;
        std::string eml_from;
        int islocal = 0;
    };

    std::vector<DownloadedEml> newEmls, exchangeEmls, dataEmls, otherEmls;

    for (const auto& pe : pending) {
        std::string filePath = accountDir + "/" + pe.uuid + ".eml";

        // islocal=1 means EML was already downloaded in fetch phase, skip download
        if (pe.islocal == 0) {
            int getResult = GetEmailToFile_c(configIndex, pe.folder.c_str(), pe.uuid.c_str(), filePath.c_str());
            if (getResult != 0) {
                LOG_INFO("[DB] download_pending: failed to fetch uid=%s folder=%s: %d\n", pe.uuid.c_str(), pe.folder.c_str(), getResult);
                if (getResult != -10) {
                    s_emailRepo.incrementRetryCount(pe.uuid, accountStr);
                }
                continue;
            }
        } else {
            LOG_INFO("[DB] download_pending: islocal=1, skipping download for uid=%s\n", pe.uuid.c_str());
        }

        std::string message_id = "";
        std::string in_reply_to = "";
        std::string x_session_chart = "";
        std::string eml_subject = "";
        std::string eml_to = "";
        std::string eml_from = "";
        std::string emlContent;

        try {
            std::ifstream emlFile(filePath);
            emlContent.assign((std::istreambuf_iterator<char>(emlFile)),
                              std::istreambuf_iterator<char>());
            emlFile.close();

            vmime::parsingContext parseCtx;
            size_t pos = 0;
            const size_t end = emlContent.size();

            auto caseInsensitiveLess = [](const std::string& a, const std::string& b) {
                return std::lexicographical_compare(
                    a.begin(), a.end(), b.begin(), b.end(),
                    [](char c1, char c2) { return ::tolower(c1) < ::tolower(c2); });
            };
            std::map<std::string, std::string, decltype(caseInsensitiveLess)> headerMap(caseInsensitiveLess);

            while (pos < end) {
                auto field = vmime::headerField::parseNext(parseCtx, emlContent, pos, end, &pos);
                if (!field) break;
                std::string fName = field->getName();
                std::string fValue;
                auto val = field->getValue();
                if (val) {
                    std::string generated;
                    vmime::utility::outputStreamStringAdapter os(generated);
                    val->generate(vmime::generationContext::getDefaultContext(), os, 0);
                    os.flush();
                    fValue = generated;
                }
                headerMap[fName] = fValue;
            }

            auto decodeHeader = [](const std::string& raw) -> std::string {
                if (raw.empty()) return raw;
                try {
                    auto decoded = vmime::text::decodeAndUnfold(raw);
                    if (decoded) {
                        return decoded->getConvertedText(vmime::charset("utf-8"));
                    }
                    return raw;
                } catch (...) {
                    return raw;
                }
            };

            auto getHeader = [&](const std::string& name) -> std::string {
                auto it = headerMap.find(name);
                if (it != headerMap.end()) return it->second;
                return "";
            };

            message_id = decodeHeader(getHeader("Message-ID"));
            in_reply_to = decodeHeader(getHeader("In-Reply-To"));

            x_session_chart = decodeHeader(getHeader("X-Mailer"));
            eml_subject = decodeHeader(getHeader("Subject"));
            eml_to = decodeHeader(getHeader("To"));
            eml_from = decodeHeader(getHeader("From"));
            std::string eml_cc = decodeHeader(getHeader("Cc"));
            std::string eml_bcc = decodeHeader(getHeader("Bcc"));

            // Extract contacts from To, Cc, Bcc into addressbook
            if (!eml_to.empty()) addressbook_extract_from_header(eml_to.c_str());
            if (!eml_cc.empty()) addressbook_extract_from_header(eml_cc.c_str());
            if (!eml_bcc.empty()) addressbook_extract_from_header(eml_bcc.c_str());
            if (!eml_from.empty()) addressbook_extract_from_header(eml_from.c_str());

            LOG_INFO("[DB] download_pending: parsed message_id='%s', in_reply_to='%s', x_mailer='%s' from %s\n",
                     message_id.c_str(), in_reply_to.c_str(), x_session_chart.c_str(), filePath.c_str());
        } catch (const std::exception& e) {
            LOG_INFO("[DB] download_pending: failed to parse .eml file: %s\n", e.what());
        }

        DownloadedEml de{pe.uuid, pe.folder, filePath, emlContent, message_id, in_reply_to, x_session_chart, eml_subject, eml_to, eml_from, pe.islocal};

        if (x_session_chart == XMailer::SESSION_INIT) {
            newEmls.push_back(de);
        } else if (x_session_chart == XMailer::PREKEY_BUNDLE) {
            exchangeEmls.push_back(de);
        } else if (x_session_chart == XMailer::RATCHET_MSG || x_session_chart == XMailer::ATTACH_META ||
                   x_session_chart == XMailer::ATTACH_CHUNK || XMailer::isMls(x_session_chart)) {
            dataEmls.push_back(de);
        } else {
            otherEmls.push_back(de);
        }
    }

    LOG_INFO("[DB] download_pending: classified - new=%zu, exchange=%zu, data=%zu, other=%zu\n",
             newEmls.size(), exchangeEmls.size(), dataEmls.size(), otherEmls.size());

    // Phase 2: Process in order: SESSION_INIT(1.0.1) → PREKEY_BUNDLE(1.0.0) → data(1.0.2/1.0.4/1.0.5) → other
    std::vector<DownloadedEml*> orderedEmls;
    for (auto& e : newEmls) orderedEmls.push_back(&e);
    for (auto& e : exchangeEmls) orderedEmls.push_back(&e);
    for (auto& e : dataEmls) orderedEmls.push_back(&e);
    for (auto& e : otherEmls) orderedEmls.push_back(&e);

    for (auto* dep : orderedEmls) {
        const auto& pe = dep->uuid;
        const std::string& filePath = dep->filePath;
        std::string& emlContent = dep->emlContent;
        std::string message_id = dep->message_id;
        std::string in_reply_to = dep->in_reply_to;
        const std::string& x_session_chart = dep->x_session_chart;
        const std::string& eml_subject = dep->eml_subject;
        const std::string& eml_to = dep->eml_to;
        const std::string& eml_from = dep->eml_from;

        // Skip if emlContent is empty (EML file missing or parse failed)
        if (emlContent.empty()) {
            LOG_INFO("[DB] download_pending: emlContent empty for uuid=%s, resetting islocal=0 for re-download\n", pe.c_str());
            s_emailRepo.setIslocal(pe, accountStr, 0);
            continue;
        }

        // Body-level ids extracted from the decrypted attachment envelopes
        // (x-message-id / x-reply-to are canonical; headers are not).
        std::string attachMsgId, attachReplyTo;

        try {

            // New Signal protocol handling (v1): PREKEY_BUNDLE (1.0.0) and SESSION_INIT/RATCHET_MSG (1.0.1/1.0.2)
            if (x_session_chart == XMailer::PREKEY_BUNDLE) {
                LOG_INFO("[DB] download_pending: X-Mailer=1.0.0 (PREKEY_BUNDLE), processing prekey bundle\n");
                std::string prekeySessionId;
                std::string bodyReplyTo;
                try {
                    vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();
                    msg->parse(emlContent);
                    std::string textBody, htmlBody;
                    json dummyAttachments = json::array();
                    bool dummyHasAttachment = false;
                    extractParts(std::static_pointer_cast<vmime::bodyPart>(msg), textBody, htmlBody, dummyAttachments, dummyHasAttachment);
                    std::string bodyText = textBody.empty() ? htmlBody : textBody;

                    auto bodyJson = json::parse(bodyText);
                    auto pb = bodyJson.value("prekey_bundle", json::object());
                    std::string ikPub = pb.value("ik_pub", "");
                    std::string spkPub = pb.value("spk_pub", "");
                    std::string spkSig = pb.value("spk_sig", "");
                    std::string opkPub = pb.value("opk_pub", "");
                    prekeySessionId = bodyJson.value("session_id", "");
                    bodyReplyTo = bodyJson.value("reply_to", "");

                    if (!ikPub.empty() && !spkPub.empty()) {
                        int rc = signal_store_peer_prekey(
                            accountStr.c_str(),
                            eml_from.c_str(),
                            ikPub.c_str(),
                            spkPub.c_str(),
                            spkSig.c_str(),
                            opkPub.c_str(),
                            prekeySessionId.c_str()
                        );
                        LOG_INFO("[DB] download_pending: signal_store_peer_prekey rc=%d\n", rc);
                    } else {
                        LOG_INFO("[DB] download_pending: PREKEY_BUNDLE missing ik_pub or spk_pub, skipping\n");
                    }
                } catch (const std::exception& e) {
                    LOG_INFO("[DB] download_pending: failed to parse PREKEY_BUNDLE body: %s\n", e.what());
                }

                // Rewrite EML with placeholder text so UI doesn't show raw JSON
                {
                    std::string placeholder = "[Signal prekey bundle - waiting for session initialization]";
                    std::string newEml;
                    newEml.reserve(placeholder.size() + 256);
                    if (!eml_from.empty()) newEml += "From: " + eml_from + "\r\n";
                    if (!eml_to.empty()) newEml += "To: " + eml_to + "\r\n";
                    if (!eml_subject.empty()) newEml += "Subject: " + eml_subject + "\r\n";
                    if (!message_id.empty()) newEml += "Message-ID: " + message_id + "\r\n";
                    if (!in_reply_to.empty()) newEml += "In-Reply-To: " + in_reply_to + "\r\n";
                    newEml += "X-Mailer: " + x_session_chart + "\r\n";
                    newEml += "MIME-Version: 1.0\r\n";
                    newEml += "Content-Type: text/plain; charset=utf-8\r\n";
                    newEml += "Content-Transfer-Encoding: 8bit\r\n";
                    newEml += "\r\n";
                    newEml += placeholder;
                    try {
                        std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
                        if (out.is_open()) {
                            out.write(newEml.data(), static_cast<std::streamsize>(newEml.size()));
                            out.close();
                            LOG_INFO("[DB] download_pending: wrote placeholder EML for PREKEY_BUNDLE uuid=%s\n", pe.c_str());
                        }
                    } catch (const std::exception& we) {
                        LOG_INFO("[DB] download_pending: failed to write placeholder EML for uuid=%s: %s\n", pe.c_str(), we.what());
                    }
                }

                // Mark as processed and continue
                s_emailRepo.updateAfterDownload(pe, accountStr, message_id, in_reply_to, pe);
                s_emailRepo.setIslocal(pe, accountStr, 2);
                LOG_INFO("[DB] download_pending: set islocal=2 for PREKEY_BUNDLE uuid=%s\n", pe.c_str());

                // Create session on receiver side for PREKEY_BUNDLE (same as SESSION_INIT)
                {
                    std::string existingSessionId;
                    // Try by reply_to from body first (most reliable for threading)
                    if (!bodyReplyTo.empty()) {
                        static SessionRepo s_sessionRepo;
                        existingSessionId = s_sessionRepo.querySessionByInReplyTo(bodyReplyTo, accountStr);
                        LOG_INFO("[DB] download_pending: PREKEY_BUNDLE lookup by body reply_to=%s -> session_id=%s\n",
                                 bodyReplyTo.c_str(), existingSessionId.c_str());
                    }
                    // Fallback: try by EML in_reply_to header
                    if (existingSessionId.empty() && !in_reply_to.empty()) {
                        static SessionRepo s_sessionRepo;
                        existingSessionId = s_sessionRepo.querySessionByInReplyTo(in_reply_to, accountStr);
                        LOG_INFO("[DB] download_pending: PREKEY_BUNDLE lookup by in_reply_to=%s -> session_id=%s\n",
                                 in_reply_to.c_str(), existingSessionId.c_str());
                    }

                    if (existingSessionId.empty()) {
                        char create_session_json[4096];
                        int create_rc = email_create_session(
                            accountStr.c_str(),
                            eml_subject.c_str(),
                            eml_from.c_str(),
                            message_id.c_str(),
                            1,
                            s_emailRepo.findRowidByUuidAndAccount(pe, accountStr),
                            create_session_json,
                            sizeof(create_session_json)
                        );
                        if (create_rc == 0) {
                            try {
                                auto resp = json::parse(create_session_json);
                                if (resp.value("status", "") == "success") {
                                    existingSessionId = resp.value("session_id", "");
                                    LOG_INFO("[DB] download_pending: PREKEY_BUNDLE created session_id=%s\n", existingSessionId.c_str());
                                }
                            } catch (...) {}
                        }
                    } else {
                        LOG_INFO("[DB] download_pending: PREKEY_BUNDLE reusing existing session_id=%s\n", existingSessionId.c_str());
                    }

                    if (!existingSessionId.empty()) {
                        int64_t emailRowid = s_emailRepo.findRowidByUuidAndAccount(pe, accountStr);
                        if (emailRowid > 0) {
                            std::string emailRowidStr = std::to_string(emailRowid);
                            char session_buffer[8192];
                            email_add_email_to_session(
                                existingSessionId.c_str(),
                                emailRowidStr.c_str(),
                                accountStr.c_str(),
                                1,
                                session_buffer,
                                sizeof(session_buffer)
                            );
                            LOG_INFO("[DB] download_pending: PREKEY_BUNDLE added email to session %s (rowid=%lld)\n", existingSessionId.c_str(), (long long)emailRowid);
                        } else {
                            LOG_INFO("[DB] download_pending: PREKEY_BUNDLE could not find rowid for uuid=%s\n", pe.c_str());
                        }

                        // Save sig_xxx from PREKEY_BUNDLE as signal_session_id for this email session
                        // ONLY if not already set — the PREKEY_BUNDLE's session_id is for future
                        // sessions, don't overwrite the existing SESSION_INIT mapping
                        if (!prekeySessionId.empty()) {
                            static SessionRepo s_sessionRepo;
                            std::string existingSigSid = s_sessionRepo.getSignalSessionId(existingSessionId);
                            if (existingSigSid.empty()) {
                                s_sessionRepo.setSignalSessionId(existingSessionId, prekeySessionId);
                                LOG_INFO("[DB] download_pending: PREKEY_BUNDLE saved signal_session_id=%s for email session=%s\n",
                                         prekeySessionId.c_str(), existingSessionId.c_str());
                            } else {
                                LOG_INFO("[DB] download_pending: PREKEY_BUNDLE keeping existing signal_session_id=%s for email session=%s (not overwriting with %s)\n",
                                         existingSigSid.c_str(), existingSessionId.c_str(), prekeySessionId.c_str());
                            }
                            // Mark that peer has responded (PREKEY_BUNDLE = peer acknowledged the session)
                            // This allows the initiator to send subsequent messages
                            static SignalSessionRepo s_signalSessionRepo;
                            s_signalSessionRepo.markReceivedBySessionId(accountStr, prekeySessionId);
                            LOG_INFO("[DB] download_pending: PREKEY_BUNDLE marked recv_n for account=%s, signal_session=%s\n",
                                     accountStr.c_str(), prekeySessionId.c_str());

                            // Key exchange for this conversation is now complete: the peer
                            // answered our SESSION_INIT, so it holds our bundle and we hold
                            // its one. Mark OUR session row — prekeySessionId travels in the
                            // body as the peer's id for *future* sessions and has no row here.
                            std::string ourSigSid = existingSigSid.empty() ? prekeySessionId : existingSigSid;
                            if (s_signalSessionRepo.markKexDone(accountStr, ourSigSid)) {
                                LOG_INFO("[DB] download_pending: PREKEY_BUNDLE key exchange complete, signal_session=%s\n",
                                         ourSigSid.c_str());
                            }
                        }
                    }
                }

                downloaded++;
                results.push_back({{"uuid", pe}, {"folder", dep->folder}, {"file", filePath}});
                continue;
            }

            // MLS protocol messages (2.0.x): invite / key_package / welcome / commit / app message.
            // Body carries x_message_id / x_reply_to; group is resolved via the chain.
            // This must run BEFORE the Signal block below, since MLS messages are not Signal messages.
            if (XMailer::isMls(x_session_chart)) {
                LOG_INFO("[DB] download_pending: MLS %s, processing\n", x_session_chart.c_str());
                try {
                    vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();
                    msg->parse(emlContent);
                    std::string textBody, htmlBody;
                    json dummyAttachments = json::array();
                    bool dummyHasAttachment = false;
                    extractParts(std::static_pointer_cast<vmime::bodyPart>(msg), textBody, htmlBody, dummyAttachments, dummyHasAttachment);
                    std::string bodyText = textBody.empty() ? htmlBody : textBody;

                    char outGroupId[128] = {0};
                    // Plaintext can be a multi-MB file chunk JSON — size the buffer to the body.
                    std::vector<char> outPlaintext(bodyText.size() * 2 + 65536);
                    int mlsRc = group_handle_incoming(
                        accountStr.c_str(), eml_from.c_str(), x_session_chart.c_str(), bodyText.c_str(),
                        message_id.c_str(), in_reply_to.c_str(),
                        outGroupId, sizeof(outGroupId), outPlaintext.data(), (int)outPlaintext.size());
                    std::string groupId(outGroupId);
                    std::string plaintext(outPlaintext.data());

                    // File transfer messages (2.0.4 meta / 2.0.5 chunk) share the file_transfer
                    // machinery with 1:1; the plaintext is the file/truck JSON.
                    if (mlsRc == 0 &&
                        (x_session_chart == XMailer::MLS_FILE_META || x_session_chart == XMailer::MLS_FILE_CHUNK)) {
                        try {
                            auto fj = json::parse(plaintext);
                            if (x_session_chart == XMailer::MLS_FILE_META && fj.value("msg_type", "") == "file") {
                                // Associate the transfer with the unified session carrying this group.
                                std::string ftSid = groupId.empty() ? "" : ("group_" + groupId);
                                {
                                    static UnifiedSessionRepo s_usRepo2;
                                    UnifiedSession us;
                                    json bodyJson = json::parse(bodyText);
                                    if (s_usRepo2.loadByRootMessageId(accountStr, bodyJson.value("x_session_id", ""), us)) {
                                        ftSid = us.sessionId;
                                    }
                                }
                                char ftResult[4096];
                                email_file_transfer_receive_file(
                                    fj.value("file_id", "").c_str(), ftSid.c_str(), accountStr.c_str(), eml_from.c_str(),
                                    fj.value("file_name", "").c_str(), fj.value("file_size", 0LL),
                                    fj.value("file_md5", "").c_str(), fj.value("total_chunks", 0),
                                    fj.value("chunk_size", 0), message_id.c_str(),
                                    fj.value("compression", "").c_str(), fj.value("compressed_md5", "").c_str(),
                                    ftResult, sizeof(ftResult));
                                LOG_INFO("[DB] download_pending: MLS file meta received, file_id=%s\n",
                                         fj.value("file_id", "").c_str());
                            } else if (x_session_chart == XMailer::MLS_FILE_CHUNK && fj.value("msg_type", "") == "truck") {
                                std::string outputDir = storageDirStr + "/" + accountStr + "/received_files";
                                std::filesystem::create_directories(outputDir);
                                char truckResult[4096];
                                email_file_transfer_receive_truck(
                                    fj.value("file_id", "").c_str(), fj.value("chunk_index", -1),
                                    fj.value("chunk_data", "").c_str(), fj.value("chunk_md5", "").c_str(),
                                    outputDir.c_str(), truckResult, sizeof(truckResult));
                                try {
                                    auto tr = json::parse(truckResult);
                                    if (tr.value("complete", false))
                                        results.push_back({{"uuid", pe}, {"folder", dep->folder}, {"file", filePath}, {"file_complete", true}, {"file_id", tr.value("file_id", "")}});
                                } catch (...) {}
                            }
                        } catch (const std::exception& e) {
                            LOG_INFO("[DB] download_pending: MLS file msg parse error: %s\n", e.what());
                        }
                    }

                    if (mlsRc == 0) {
                        // Rewrite the local .eml with the plaintext (or a placeholder for control
                        // messages and file chunks — never store multi-MB bodies locally).
                        std::string display;
                        if (x_session_chart == XMailer::MLS_FILE_CHUNK) {
                            display = "[file chunk]";
                        } else if (plaintext.empty()) {
                            display = std::string("[MLS handshake: ") + x_session_chart + "]";
                        } else {
                            display = plaintext;
                        }
                        std::string newEml;
                        newEml.reserve(display.size() + 256);
                        if (!eml_from.empty()) newEml += "From: " + eml_from + "\r\n";
                        if (!eml_to.empty()) newEml += "To: " + eml_to + "\r\n";
                        if (!eml_subject.empty()) newEml += "Subject: " + eml_subject + "\r\n";
                        if (!message_id.empty()) newEml += "Message-ID: " + message_id + "\r\n";
                        if (!in_reply_to.empty()) newEml += "In-Reply-To: " + in_reply_to + "\r\n";
                        newEml += "X-Mailer: " + x_session_chart + "\r\n";
                        if (!groupId.empty()) newEml += "X-Group-Id: " + groupId + "\r\n";
                        newEml += "MIME-Version: 1.0\r\n";
                        newEml += "Content-Type: text/plain; charset=utf-8\r\n";
                        newEml += "Content-Transfer-Encoding: 8bit\r\n";
                        newEml += "\r\n";
                        newEml += display;
                        std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
                        if (out.is_open()) { out.write(newEml.data(), (std::streamsize)newEml.size()); out.close(); }

                        if (!groupId.empty()) {
                            int64_t emailId = s_emailRepo.findRowidByUuidAndAccount(pe, accountStr);
                            if (emailId > 0) {
                                std::string sid = "group_" + groupId;
                                char sr[4096];
                                email_add_email_to_session(sid.c_str(), std::to_string(emailId).c_str(),
                                                           accountStr.c_str(), 1, sr, sizeof(sr));
                                LOG_INFO("[DB] download_pending: MLS %s added to session=%s\n", x_session_chart.c_str(), sid.c_str());
                            }
                        }
                        s_emailRepo.updateAfterDownload(pe, accountStr, message_id, in_reply_to, pe);
                        s_emailRepo.setIslocal(pe, accountStr, 2);
                        downloaded++;
                        results.push_back({{"uuid", pe}, {"folder", dep->folder}, {"file", filePath}});
                        continue;
                    } else if (mlsRc == 1) {
                        // Retry later (e.g. Commit arrived before Welcome)
                        LOG_INFO("[DB] download_pending: MLS %s deferred (rc=1), will retry\n", x_session_chart.c_str());
                        s_emailRepo.incrementRetryCount(pe, accountStr);
                        // Keep islocal as-is so it is picked up again
                        continue;
                    } else {
                        LOG_INFO("[DB] download_pending: MLS %s failed rc=%d\n", x_session_chart.c_str(), mlsRc);
                    }
                } catch (const std::exception& e) {
                    LOG_INFO("[DB] download_pending: MLS processing failed: %s\n", e.what());
                }
            }

            if (x_session_chart == XMailer::SESSION_INIT || x_session_chart == XMailer::RATCHET_MSG) {
            LOG_INFO("[DB] download_pending: X-Mailer=%s (Signal message), decrypting via signal_session_decrypt\n", x_session_chart.c_str());
            std::string signalInReplyTo;
            std::string signalMessageId;
            std::string signalSessionIdFromDecrypt;
            try {
                vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();
                msg->parse(emlContent);
                std::string textBody, htmlBody;
                json dummyAttachments = json::array();
                bool dummyHasAttachment = false;
                extractParts(std::static_pointer_cast<vmime::bodyPart>(msg), textBody, htmlBody, dummyAttachments, dummyHasAttachment);
                std::string bodyText = textBody.empty() ? htmlBody : textBody;

                std::vector<char> outBuf(65536);
                int rc = signal_session_decrypt(
                    accountStr.c_str(),
                    eml_from.c_str(),
                    bodyText.c_str(),
                    outBuf.data(),
                    (int)outBuf.size()
                );

                if (rc == 0) {
                    std::string respStr(outBuf.data());
                    LOG_INFO("[DB] download_pending: signal_session_decrypt resp=%s\n", respStr.c_str());
                    try {
                        auto resp = json::parse(respStr);
                        signalInReplyTo = resp.value("x_reply_to", "");
                        signalMessageId = resp.value("x_message_id", "");
                        if (resp.value("status", "") == "success") {
                            std::string plaintext = resp.value("plaintext", "");
                            signalSessionIdFromDecrypt = resp.value("session_id", "");
                            LOG_INFO("[DB] download_pending: Signal decrypt success, session_id=%s, plaintext_len=%zu, body message_id=%s, in_reply_to=%s\n",
                                     signalSessionIdFromDecrypt.c_str(), plaintext.size(), signalMessageId.c_str(), signalInReplyTo.c_str());

                            // Replace emlContent with a minimal text/plain message containing the decrypted plaintext
                            emlContent.clear();
                            emlContent.reserve(plaintext.size() + 256);
                            if (!eml_from.empty()) {
                                emlContent += "From: " + eml_from + "\r\n";
                            }
                            if (!eml_to.empty()) {
                                emlContent += "To: " + eml_to + "\r\n";
                            }
                            if (!eml_subject.empty()) {
                                emlContent += "Subject: " + eml_subject + "\r\n";
                            }
                            if (!message_id.empty()) {
                                emlContent += "Message-ID: " + message_id + "\r\n";
                            }
                            if (!in_reply_to.empty()) {
                                emlContent += "In-Reply-To: " + in_reply_to + "\r\n";
                            }
                            emlContent += "X-Mailer: " + x_session_chart + "\r\n";
                            emlContent += "MIME-Version: 1.0\r\n";
                            emlContent += "Content-Type: text/plain; charset=utf-8\r\n";
                            emlContent += "Content-Transfer-Encoding: 8bit\r\n";
                            emlContent += "\r\n";
                            emlContent += plaintext;

                            // Persist updated EML back to disk so UI can render decrypted text
                            try {
                                std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
                                if (out.is_open()) {
                                    out.write(emlContent.data(), static_cast<std::streamsize>(emlContent.size()));
                                    out.close();
                                    LOG_INFO("[DB] download_pending: wrote decrypted EML for uuid=%s to %s\n", pe.c_str(), filePath.c_str());
                                } else {
                                    LOG_INFO("[DB] download_pending: failed to open EML for writing (uuid=%s, path=%s)\n", pe.c_str(), filePath.c_str());
                                }
                            } catch (const std::exception& we) {
                                LOG_INFO("[DB] download_pending: exception while writing decrypted EML for uuid=%s: %s\n", pe.c_str(), we.what());
                            }
                        } else {
                            std::string err = resp.value("error", "");
                            LOG_INFO("[DB] download_pending: Signal decrypt error: %s, body message_id=%s, in_reply_to=%s\n", err.c_str(), signalMessageId.c_str(), signalInReplyTo.c_str());
                        }
                    } catch (const std::exception& e) {
                        LOG_INFO("[DB] download_pending: failed to parse signal decrypt response JSON: %s\n", e.what());
                    }
                } else {
                    // Even on decrypt failure, try to extract fields from raw body for session matching
                    try {
                        auto rawJson = json::parse(bodyText);
                        signalInReplyTo = rawJson.value("x_reply_to", "");
                        signalMessageId = rawJson.value("x_message_id", "");
                        signalSessionIdFromDecrypt = rawJson.value("session_id", "");
                        LOG_INFO("[DB] download_pending: signal_session_decrypt failed, rc=%d, extracted body message_id=%s, in_reply_to=%s, session_id=%s from raw body\n", rc, signalMessageId.c_str(), signalInReplyTo.c_str(), signalSessionIdFromDecrypt.c_str());
                    } catch (...) {
                        LOG_INFO("[DB] download_pending: signal_session_decrypt failed, rc=%d, could not extract body fields\n", rc);
                    }
                }
            } catch (const std::exception& e) {
                LOG_INFO("[DB] download_pending: failed to decrypt Signal message: %s\n", e.what());
            }

            // SESSION_INIT: create or reuse session on receiver side
            if (x_session_chart == XMailer::SESSION_INIT) {
                LOG_INFO("[DB] download_pending: SESSION_INIT, creating session on receiver side\n");
                try {
                    std::string existingSessionId;

                    // Primary: use in_reply_to from Signal body (message_id of the email being replied to)
                    if (!signalInReplyTo.empty()) {
                        char existingSid[512];
                        existingSid[0] = '\0';
                        email_query_session_by_message_id(signalInReplyTo.c_str(), accountStr.c_str(), existingSid, sizeof(existingSid));
                        existingSessionId = existingSid;
                        if (!existingSessionId.empty()) {
                            LOG_INFO("[DB] download_pending: SESSION_INIT found session by body in_reply_to=%s -> %s\n", signalInReplyTo.c_str(), existingSessionId.c_str());
                        }
                    }

                    // Fallback 1: try email header message_id
                    if (existingSessionId.empty()) {
                        char existingSid[512];
                        existingSid[0] = '\0';
                        email_query_session_by_message_id(message_id.c_str(), accountStr.c_str(), existingSid, sizeof(existingSid));
                        existingSessionId = existingSid;
                    }

                    // For SESSION_INIT (new conversation), always create a new session
                    // on the receiver side. Don't use subject+sender fallback as it
                    // could match a cross-account session.

                    std::string newSessionId;
                    if (!existingSessionId.empty()) {
                        newSessionId = existingSessionId;
                        LOG_INFO("[DB] download_pending: reusing existing session_id=%s\n", newSessionId.c_str());
                    } else {
                        char create_session_json[4096];
                        int create_rc = email_create_session(
                            accountStr.c_str(),
                            eml_subject.c_str(),
                            eml_from.c_str(),
                            message_id.c_str(),
                            1, // encrypt_method=1 for Signal
                            s_emailRepo.findRowidByUuidAndAccount(pe, accountStr),
                            create_session_json,
                            sizeof(create_session_json)
                        );
                        LOG_INFO("[DB] download_pending: email_create_session result=%d\n", create_rc);
                        if (create_rc == 0) {
                            try {
                                auto createResp = json::parse(create_session_json);
                                if (createResp.value("status", "") == "success") {
                                    newSessionId = createResp.value("session_id", "");
                                }
                            } catch (...) {}
                        }
                    }

                    if (!newSessionId.empty()) {
                        int64_t emailId = s_emailRepo.findRowidByUuidAndAccount(pe, accountStr);
                        if (emailId > 0) {
                            std::string emailIdStr = std::to_string(emailId);
                            char session_result_json[4096];
                            email_add_email_to_session(
                                newSessionId.c_str(), emailIdStr.c_str(),
                                accountStr.c_str(), 1,
                                session_result_json, sizeof(session_result_json));
                            LOG_INFO("[DB] download_pending: SESSION_INIT email added to session=%s\n", newSessionId.c_str());
                        }
                        // Save Signal session_id mapping
                        if (!signalSessionIdFromDecrypt.empty()) {
                            s_sessionRepo.setSignalSessionId(newSessionId, signalSessionIdFromDecrypt);
                            LOG_INFO("[DB] download_pending: saved signal_session_id=%s for email session=%s\n",
                                     signalSessionIdFromDecrypt.c_str(), newSessionId.c_str());
                        }

                        // Create unified session record so this conversation appears in the
                        // unified session list (not just the legacy list).
                        {
                            static UnifiedSessionRepo s_usRepo;
                            // Dedup by the conversation root (the INIT's x_message_id), not
                            // by member set — a new SESSION_INIT from the same peer is a new
                            // session and must get its own unified record.
                            std::string usRoot = signalMessageId.empty() ? message_id : signalMessageId;
                            std::vector<std::string> usMembers = {accountStr, eml_from};
                            UnifiedSession existingUs;
                            if (usRoot.empty() || !s_usRepo.loadByRootMessageId(accountStr, usRoot, existingUs)) {
                                UnifiedSession usRec;
                                // Generate us_<secs>.<random> id (same format as UnifiedSessionManager)
                                static std::mt19937_64 rng{std::random_device{}()};
                                auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch()).count();
                                usRec.sessionId = "us_" + std::to_string(secs) + "." + std::to_string(rng() % 100000000);
                                usRec.account = accountStr;
                                usRec.subject = eml_subject;
                                usRec.mode = SessionMode::Signal;
                                usRec.members = usMembers;
                                usRec.signalSessionId = signalSessionIdFromDecrypt;
                                usRec.rootMessageId = usRoot;
                                usRec.status = 0;
                                if (s_usRepo.create(usRec)) {
                                    LOG_INFO("[DB] download_pending: created unified session=%s for received SESSION_INIT\n",
                                             usRec.sessionId.c_str());
                                } else {
                                    LOG_INFO("[DB] download_pending: failed to create unified session for received SESSION_INIT\n");
                                }
                            }
                        }

                        // Send our PREKEY_BUNDLE (1.0.0) back to the initiator so they can
                        // initiate future sessions with our latest prekeys.
                        {
                            char prekeyBuf[65536];
                            int prekeyRc = signal_get_prekey_bundle(accountStr.c_str(), prekeyBuf, sizeof(prekeyBuf));
                            if (prekeyRc == 0) {
                                std::string prekeyBody(prekeyBuf);
                                // Add reply_to to body so receiver can associate with the correct session
                                try {
                                    auto pkJson = json::parse(prekeyBody);
                                    pkJson["reply_to"] = message_id;
                                    prekeyBody = pkJson.dump();
                                } catch (...) {}
                                std::string prekeySubject = "Re: " + eml_subject;
                                std::string prekeyReplyTo = message_id;
                                // Generate a unique message_id for the prekey bundle email
                                char prekeyMsgId[256];
                                snprintf(prekeyMsgId, sizeof(prekeyMsgId), "%lld.%s@oim",
                                         (long long)time(nullptr), accountStr.c_str());

                                int64_t taskId = s_taskRepo.insert(
                                    accountStr, eml_from, prekeySubject, prekeyBody,
                                    prekeyReplyTo, prekeyMsgId, "", "",
                                    XMailer::PREKEY_BUNDLE);
                                if (taskId > 0) {
                                    LOG_INFO("[DB] download_pending: queued PREKEY_BUNDLE (1.0.0) to %s, task_id=%lld\n",
                                             eml_from.c_str(), (long long)taskId);
                                    // We are the responder: decrypting the SESSION_INIT gave us
                                    // the initiator's keys, and our own bundle is now handed to
                                    // the transport for it. Both directions are covered.
                                    static SignalSessionRepo s_signalSessionRepo;
                                    if (s_signalSessionRepo.markKexDone(accountStr, signalSessionIdFromDecrypt)) {
                                        LOG_INFO("[DB] download_pending: SESSION_INIT reply queued, key exchange complete, signal_session=%s\n",
                                                 signalSessionIdFromDecrypt.c_str());
                                    }
                                } else {
                                    LOG_INFO("[DB] download_pending: failed to queue PREKEY_BUNDLE task\n");
                                }
                            } else {
                                LOG_INFO("[DB] download_pending: signal_get_prekey_bundle failed rc=%d, skipping PREKEY_BUNDLE reply\n", prekeyRc);
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    LOG_INFO("[DB] download_pending: SESSION_INIT session creation failed: %s\n", e.what());
                }
            }

            // RATCHET_MSG: associate email with existing session
            if (x_session_chart == XMailer::RATCHET_MSG) {
                try {
                    std::string existingSessionId;

                    // Use in_reply_to from Signal body to find session
                    if (!signalInReplyTo.empty()) {
                        char existingSid[512];
                        existingSid[0] = '\0';
                        email_query_session_by_message_id(signalInReplyTo.c_str(), accountStr.c_str(), existingSid, sizeof(existingSid));
                        existingSessionId = existingSid;
                    }

                    // Fallback 1: try email header in_reply_to
                    if (existingSessionId.empty() && !in_reply_to.empty()) {
                        char existingSid[512];
                        existingSid[0] = '\0';
                        email_query_session_by_message_id(in_reply_to.c_str(), accountStr.c_str(), existingSid, sizeof(existingSid));
                        existingSessionId = existingSid;
                    }

                    // Fallback 2: use Signal session_id mapping when Message-Id has been rewritten by server
                    if (existingSessionId.empty() && !signalSessionIdFromDecrypt.empty()) {
                        std::string sidBySignal = s_sessionRepo.findSessionIdBySignalSessionId(accountStr, signalSessionIdFromDecrypt);
                        if (!sidBySignal.empty()) {
                            existingSessionId = sidBySignal;
                            LOG_INFO("[DB] download_pending: RATCHET_MSG found session by signal_session_id=%s -> %s\n",
                                     signalSessionIdFromDecrypt.c_str(), existingSessionId.c_str());
                        }
                    }

                    if (!existingSessionId.empty()) {
                        int64_t emailId = s_emailRepo.findRowidByUuidAndAccount(pe, accountStr);
                        if (emailId > 0) {
                            std::string emailIdStr = std::to_string(emailId);
                            char session_result_json[4096];
                            email_add_email_to_session(
                                existingSessionId.c_str(), emailIdStr.c_str(),
                                accountStr.c_str(), 1,
                                session_result_json, sizeof(session_result_json));
                            LOG_INFO("[DB] download_pending: RATCHET_MSG email added to session=%s\n", existingSessionId.c_str());
                        }
                    } else {
                        LOG_INFO("[DB] download_pending: RATCHET_MSG could not find session for in_reply_to=%s (signal_session_id=%s)\n",
                                 signalInReplyTo.c_str(), signalSessionIdFromDecrypt.c_str());
                    }
                } catch (const std::exception& e) {
                    LOG_INFO("[DB] download_pending: RATCHET_MSG session association failed: %s\n", e.what());
                }
            }

            // Mark as processed. Store the sender's locally generated message_id / in_reply_to
            // (from the Signal envelope) so the reply chain matches the sender's own records even
            // when the mail server rewrote the Message-ID header.
            {
                const std::string& storeMsgId = signalMessageId.empty() ? message_id : signalMessageId;
                const std::string& storeInReplyTo = signalInReplyTo.empty() ? in_reply_to : signalInReplyTo;
                s_emailRepo.updateAfterDownload(pe, accountStr, storeMsgId, storeInReplyTo, pe);
            }
            s_emailRepo.setIslocal(pe, accountStr, 2);
            LOG_INFO("[DB] download_pending: set islocal=2 for Signal uuid=%s\n", pe.c_str());
            downloaded++;
            results.push_back({{"uuid", pe}, {"folder", dep->folder}, {"file", filePath}});
            continue;
        }

            // ATTACH_META (1.0.4): File metadata (visible in UI) — decrypt via Signal
            if (x_session_chart == XMailer::ATTACH_META) {
                LOG_INFO("[DB] download_pending: ATTACH_META, decrypting via Signal\n");
                try {
                    vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();
                    msg->parse(emlContent);
                    std::string textBody, htmlBody;
                    json dummyAttachments = json::array();
                    bool dummyHasAttachment = false;
                    extractParts(std::static_pointer_cast<vmime::bodyPart>(msg), textBody, htmlBody, dummyAttachments, dummyHasAttachment);
                    std::string bodyText = textBody.empty() ? htmlBody : textBody;

                    std::vector<char> decJson(bodyText.size() + 65536);
                    int decRc = signal_session_decrypt(accountStr.c_str(), eml_from.c_str(), bodyText.c_str(), decJson.data(), (int)decJson.size());
                    if (decRc == 0) {
                        auto decResp = json::parse(decJson.data());
                        if (decResp.value("status", "") == "success") {
                            auto fileJson = json::parse(decResp.value("plaintext", ""));
                            attachMsgId = fileJson.value("x_message_id", "");
                            attachReplyTo = fileJson.value("x_reply_to", "");
                            std::string sid;
                            if (!in_reply_to.empty()) sid = s_sessionRepo.querySessionByInReplyTo(in_reply_to, accountStr);
                            char ftResult[4096];
                            email_file_transfer_receive_file(
                                fileJson.value("file_id","").c_str(), sid.c_str(), accountStr.c_str(), eml_from.c_str(),
                                fileJson.value("file_name","").c_str(), fileJson.value("file_size",0LL),
                                fileJson.value("file_md5","").c_str(), fileJson.value("total_chunks",0),
                                fileJson.value("chunk_size",0), message_id.c_str(),
                                fileJson.value("compression","").c_str(), fileJson.value("compressed_md5","").c_str(),
                                ftResult, sizeof(ftResult));
                            if (!sid.empty()) {
                                int64_t emailId = s_emailRepo.findRowidByUuidAndAccount(pe, accountStr);
                                if (emailId > 0) {
                                    char sr[4096]; email_add_email_to_session(sid.c_str(), std::to_string(emailId).c_str(), accountStr.c_str(), 1, sr, sizeof(sr));
                                }
                            }
                            // Rewrite the local .eml with the plaintext file-meta JSON so the UI can
                            // render the file card (the sender cannot be re-asked; DR keys are one-shot).
                            {
                                std::string plain = decResp.value("plaintext", "");
                                std::string newEml;
                                newEml.reserve(plain.size() + 256);
                                if (!eml_from.empty()) newEml += "From: " + eml_from + "\r\n";
                                if (!eml_to.empty()) newEml += "To: " + eml_to + "\r\n";
                                if (!eml_subject.empty()) newEml += "Subject: " + eml_subject + "\r\n";
                                if (!message_id.empty()) newEml += "Message-ID: " + message_id + "\r\n";
                                if (!in_reply_to.empty()) newEml += "In-Reply-To: " + in_reply_to + "\r\n";
                                newEml += "X-Mailer: " + x_session_chart + "\r\n";
                                newEml += "MIME-Version: 1.0\r\n";
                                newEml += "Content-Type: text/plain; charset=utf-8\r\n";
                                newEml += "Content-Transfer-Encoding: 8bit\r\n";
                                newEml += "\r\n";
                                newEml += plain;
                                std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
                                if (out.is_open()) { out.write(newEml.data(), (std::streamsize)newEml.size()); out.close(); }
                            }
                        }
                    } else LOG_INFO("[DB] download_pending: ATTACH_META decrypt failed rc=%d\n", decRc);
                } catch (const std::exception& e) { LOG_INFO("[DB] download_pending: ATTACH_META error: %s\n", e.what()); }
            }

            // ATTACH_CHUNK (1.0.5): File chunk (hidden from UI) — decrypt via Signal
            if (x_session_chart == XMailer::ATTACH_CHUNK) {
                LOG_INFO("[DB] download_pending: ATTACH_CHUNK, decrypting via Signal\n");
                try {
                    vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();
                    msg->parse(emlContent);
                    std::string textBody, htmlBody;
                    json dummyAttachments = json::array();
                    bool dummyHasAttachment = false;
                    extractParts(std::static_pointer_cast<vmime::bodyPart>(msg), textBody, htmlBody, dummyAttachments, dummyHasAttachment);
                    std::string bodyText = textBody.empty() ? htmlBody : textBody;

                    std::vector<char> decJson(bodyText.size() + 65536);
                    int decRc = signal_session_decrypt(accountStr.c_str(), eml_from.c_str(), bodyText.c_str(), decJson.data(), (int)decJson.size());
                    if (decRc == 0) {
                        auto decResp = json::parse(decJson.data());
                        if (decResp.value("status", "") == "success") {
                            auto truckJson = json::parse(decResp.value("plaintext", ""));
                            attachMsgId = truckJson.value("x_message_id", "");
                            attachReplyTo = truckJson.value("x_reply_to", "");
                            std::string outputDir = storageDirStr + "/" + accountStr + "/received_files";
                            std::filesystem::create_directories(outputDir);
                            char truckResult[4096];
                            email_file_transfer_receive_truck(
                                truckJson.value("file_id","").c_str(), truckJson.value("chunk_index",-1),
                                truckJson.value("chunk_data","").c_str(), truckJson.value("chunk_md5","").c_str(),
                                outputDir.c_str(), truckResult, sizeof(truckResult));
                            try {
                                auto tr = json::parse(truckResult);
                                if (tr.value("complete", false))
                                    results.push_back({{"uuid",pe},{"folder",dep->folder},{"file",filePath},{"file_complete",true},{"file_id",tr.value("file_id","")}});
                            } catch (...) {}
                        }
                    } else LOG_INFO("[DB] download_pending: ATTACH_CHUNK decrypt failed rc=%d\n", decRc);
                } catch (const std::exception& e) { LOG_INFO("[DB] download_pending: ATTACH_CHUNK error: %s\n", e.what()); }
            }

            // Session association via embedded IDs / in_reply_to for non-attachment types
            // 优先使用 body 里的 x-message-id / x-reply-id / last_message_id，
            // 再回退到 Header 的 In-Reply-To。
            if (x_session_chart != XMailer::ATTACH_CHUNK && x_session_chart != XMailer::RATCHET_MSG && x_session_chart != XMailer::ATTACH_META) {
                std::string replyIdFromBody;

                // Try to parse text body as JSON and extract stable reply id fields
                try {
                    vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();
                    msg->parse(emlContent);

                    std::string textBody;
                    std::string htmlBody;
                    json dummyAttachments = json::array();
                    bool dummyHasAttachment = false;
                    extractParts(std::static_pointer_cast<vmime::bodyPart>(msg), textBody, htmlBody, dummyAttachments, dummyHasAttachment);
                    std::string bodyText = textBody.empty() ? htmlBody : textBody;

                    if (!bodyText.empty()) {
                        try {
                            auto bodyJson = json::parse(bodyText);

                            // x-reply-id style: allow multiple spellings and legacy last_message_id
                            if (bodyJson.contains("x_reply_id") && bodyJson["x_reply_id"].is_string()) {
                                replyIdFromBody = bodyJson["x_reply_id"].get<std::string>();
                            } else if (bodyJson.contains("x-reply-id") && bodyJson["x-reply-id"].is_string()) {
                                replyIdFromBody = bodyJson["x-reply-id"].get<std::string>();
                            } else if (bodyJson.contains("last_message_id") && bodyJson["last_message_id"].is_string()) {
                                replyIdFromBody = bodyJson["last_message_id"].get<std::string>();
                            }
                        } catch (...) {
                            // Body is not JSON, ignore
                        }
                    }
                } catch (...) {
                    // Parsing body failed, ignore and fall back to header
                }

                // Prefer body-level reply id; fall back to header In-Reply-To
                auto tryLinkWithId = [&](const std::string& replyId, const char* sourceLabel) {
                    if (replyId.empty()) return false;
                    char foundSid[512];
                    foundSid[0] = '\0';
                    email_query_session_by_message_id(replyId.c_str(), accountStr.c_str(), foundSid, sizeof(foundSid));
                    if (foundSid[0] != '\0') {
                        int64_t emailId = s_emailRepo.findRowidByUuidAndAccount(pe, accountStr);
                        if (emailId > 0) {
                            std::string emailIdStr = std::to_string(emailId);
                            char session_result_json[4096];
                            int add_rc = email_add_email_to_session(
                                foundSid, emailIdStr.c_str(), accountStr.c_str(), 0,
                                session_result_json, sizeof(session_result_json));
                            LOG_INFO("[DB] download_pending: data email added to session=%s via %s reply_id=%s, email_id=%s, result=%d\n",
                                     foundSid, sourceLabel, replyId.c_str(), emailIdStr.c_str(), add_rc);
                            return true;
                        }
                    }
                    return false;
                };

                bool linked = false;
                // 1. Body JSON x-reply-id / last_message_id
                linked = tryLinkWithId(replyIdFromBody, "body");

                // 2. Fallback: header In-Reply-To
                if (!linked && !in_reply_to.empty()) {
                    linked = tryLinkWithId(in_reply_to, "header");
                }

                if (!linked) {
                    LOG_INFO("[DB] download_pending: data email - could not find session for reply_id(body='%s', header='%s'), account=%s\n",
                             replyIdFromBody.c_str(), in_reply_to.c_str(), accountStr.c_str());
                }
            }

        } catch (const std::exception& e) {
            LOG_INFO("[DB] download_pending: failed to parse .eml file: %s\n", e.what());
        }

        // Update localemail with final message_id and in_reply_to (and the .eml file
        // name — the only writer of the `file` column).
        // RATCHET_MSG: embedded IDs were already persisted in its block above.
        // ATTACH_META/CHUNK: use the body-level x-message-id/x-reply-to extracted
        // during decryption, falling back to envelope headers.
        if (x_session_chart == XMailer::ATTACH_META || x_session_chart == XMailer::ATTACH_CHUNK) {
            const std::string& m = attachMsgId.empty() ? message_id : attachMsgId;
            const std::string& r = attachReplyTo.empty() ? in_reply_to : attachReplyTo;
            s_emailRepo.updateAfterDownload(pe, accountStr, m, r, pe);
        } else if (x_session_chart != XMailer::RATCHET_MSG) {
            s_emailRepo.updateAfterDownload(pe, accountStr, message_id, in_reply_to, pe);
        }

        // Mark as fully processed to prevent reprocessing
        s_emailRepo.setIslocal(pe, accountStr, 2);
        LOG_INFO("[DB] download_pending: set islocal=2 for uuid=%s\n", pe.c_str());

        downloaded++;
        results.push_back({{"uuid", pe}, {"folder", dep->folder}, {"file", filePath}});
        LOG_INFO("[DB] download_pending: saved uid=%s to %s\n", pe.c_str(), filePath.c_str());
    }

    json response;
    response["status"] = "success";
    response["downloaded"] = downloaded;
    response["results"] = results;

    std::string jsonStr = response.dump();
    if (outJson && outSize > 0) {
        snprintf(outJson, outSize, "%s", jsonStr.c_str());
    }

    return downloaded;
}

// Helper: recursively extract text/html body and attachments from vmime message parts
static void extractParts(const vmime::shared_ptr<vmime::bodyPart>& part,
                         std::string& textBody, std::string& htmlBody,
                         json& attachments, bool& hasAttachment) {
    auto body = part->getBody();
    vmime::mediaType ct = body->getContentType();

    if (ct.getType() == vmime::mediaTypes::MULTIPART) {
        auto parts = body->getPartList();
        for (size_t i = 0; i < parts.size(); i++) {
            extractParts(parts[i], textBody, htmlBody, attachments, hasAttachment);
        }
        return;
    }

    bool isAttachment = false;
    std::string filename;

    if (part->getHeader()->hasField(vmime::fields::CONTENT_DISPOSITION)) {
        auto cdf = part->getHeader()->findField<vmime::contentDispositionField>(vmime::fields::CONTENT_DISPOSITION);
        if (cdf) {
            auto disp = cdf->getValue<vmime::contentDisposition>();
            if (disp && disp->getName() != vmime::contentDispositionTypes::INLINE) {
                isAttachment = true;
                auto cdfField = vmime::dynamicCast<vmime::contentDispositionField>(cdf);
                if (cdfField && cdfField->hasFilename()) {
                    filename = cdfField->getFilename().getBuffer();
                }
            }
        }
    }

    auto mainType = ct.getType();
    auto subType = ct.getSubType();

    if (mainType != vmime::mediaTypes::TEXT && !isAttachment) {
        isAttachment = true;
        if (filename.empty()) filename = "unknown";
    }

    if (isAttachment) {
        if (filename.empty()) {
            if (part->getHeader()->hasField(vmime::fields::CONTENT_TYPE)) {
                auto ctf = part->getHeader()->findField<vmime::contentTypeField>(vmime::fields::CONTENT_TYPE);
                if (ctf && ctf->hasParameter("name")) {
                    auto nameParam = ctf->getParameter("name");
                    if (nameParam) filename = nameParam->getValue().getBuffer();
                }
            }
            if (filename.empty()) filename = "unknown";
        }

        std::string data;
        vmime::utility::outputStreamStringAdapter osa(data);
        body->getContents()->extract(osa);
        osa.flush();

        json att;
        att["filename"] = filename;
        att["content_type"] = ct.generate();
        att["size"] = (int)data.size();
        attachments.push_back(att);
        hasAttachment = true;
        return;
    }

    if (mainType == vmime::mediaTypes::TEXT) {
        std::string content;
        vmime::utility::outputStreamStringAdapter osa(content);
        body->getContents()->extract(osa);
        osa.flush();

        // Fallback: if Content-Transfer-Encoding was stripped by server (e.g. QQ),
        // vmime won't decode QP. Detect raw QP content and decode manually.
        if (!content.empty()) {
            int qpCount = 0;
            for (size_t i = 0; i + 2 < content.size(); i++) {
                if (content[i] == '=' && std::isxdigit((unsigned char)content[i+1]) && std::isxdigit((unsigned char)content[i+2])) {
                    qpCount++;
                }
            }
            if (qpCount >= 3) {
                std::string decoded;
                decoded.reserve(content.size());
                for (size_t i = 0; i < content.size(); i++) {
                    if (content[i] == '=' && i + 2 < content.size()) {
                        if (content[i+1] == '\r' && content[i+2] == '\n') {
                            i += 2;
                        } else if (content[i+1] == '\n') {
                            i += 1;
                        } else if (std::isxdigit((unsigned char)content[i+1]) && std::isxdigit((unsigned char)content[i+2])) {
                            char hex[3] = {content[i+1], content[i+2], '\0'};
                            decoded += (char)strtol(hex, nullptr, 16);
                            i += 2;
                        } else {
                            decoded += content[i];
                        }
                    } else {
                        decoded += content[i];
                    }
                }
                content = decoded;
            }
        }

        vmime::charset charset = body->getCharset();
        if (charset.getName() != vmime::charsets::UTF_8) {
            try {
                vmime::shared_ptr<vmime::charsetConverter> conv =
                    vmime::charsetConverter::create(charset, vmime::charset(vmime::charsets::UTF_8));
                std::string converted;
                conv->convert(content, converted);
                content = converted;
            } catch (...) {
            }
        }

        if (subType == vmime::mediaTypes::TEXT_PLAIN && textBody.empty()) {
            textBody = content;
        } else if (subType == vmime::mediaTypes::TEXT_HTML && htmlBody.empty()) {
            htmlBody = content;
        }
    }
}

extern "C" int email_parse_eml(const char* filePath, char* outJson, int outSize) {
    if (!filePath || !outJson || outSize <= 0) {
        if (outJson && outSize > 0) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"null_parameter"})");
        }
        return -1;
    }

    try {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"file_not_found"})");
            return -2;
        }

        std::ostringstream oss;
        oss << file.rdbuf();
        std::string content = oss.str();
        file.close();

        vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();
        msg->parse(content);

        std::string subject;
        std::string xMailer;
        auto header = msg->getHeader();
        if (header->hasField("Subject")) {
            auto subjField = header->findField(vmime::fields::SUBJECT);
            if (subjField) {
                auto val = subjField->getValue<vmime::text>();
                if (val) {
                    subject = val->getConvertedText(vmime::charset(vmime::charsets::UTF_8));
                }
            }
        }
        if (header->hasField("X-Mailer")) {
            auto xMailerField = header->findField("X-Mailer");
            if (xMailerField) {
                auto val = xMailerField->getValue<vmime::text>();
                if (val) {
                    xMailer = val->getConvertedText(vmime::charset(vmime::charsets::UTF_8));
                }
            }
        }

        std::string textBody;
        std::string htmlBody;
        json attachments = json::array();
        bool hasAttachment = false;

        extractParts(msg, textBody, htmlBody, attachments, hasAttachment);

        json response;
        response["status"] = "success";
        response["subject"] = subject;
        response["text_body"] = textBody;
        response["html_body"] = htmlBody;
        response["has_attachments"] = hasAttachment;
        response["attachments"] = attachments;
        response["x_mailer"] = xMailer;

        std::string jsonStr = response.dump();
        if ((int)jsonStr.size() >= outSize) {
            snprintf(outJson, outSize, R"({"status":"failed","error":"output_too_small","needed":%d})", (int)jsonStr.size());
            return -3;
        }
        snprintf(outJson, outSize, "%s", jsonStr.c_str());

        LOG_INFO("email_parse_eml: parsed %s, text=%zu bytes, html=%zu bytes, attachments=%zu\n",
                 filePath, textBody.size(), htmlBody.size(), attachments.size());
        return 0;
    } catch (const vmime::exception& e) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"vmime_exception"})");
        LOG_INFO("email_parse_eml: vmime exception: %s\n", e.what());
        return -4;
    } catch (const std::exception& e) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"std_exception"})");
        LOG_INFO("email_parse_eml: std exception: %s\n", e.what());
        return -5;
    } catch (...) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"unknown_exception"})");
        return -6;
    }
}

// Helper: extract attachment by index and write to file
static bool extractAttachmentToFile(vmime::shared_ptr<vmime::bodyPart> part,
                                     int targetIndex, int& currentIndex,
                                     const std::string& outputPath) {
    auto body = part->getBody();
    vmime::mediaType ct = body->getContentType();

    if (ct.getType() == vmime::mediaTypes::MULTIPART) {
        auto parts = body->getPartList();
        for (size_t i = 0; i < parts.size(); i++) {
            if (extractAttachmentToFile(parts[i], targetIndex, currentIndex, outputPath))
                return true;
        }
        return false;
    }

    bool isAttachment = false;
    std::string filename;

    if (part->getHeader()->hasField(vmime::fields::CONTENT_DISPOSITION)) {
        auto cdf = part->getHeader()->findField<vmime::contentDispositionField>(vmime::fields::CONTENT_DISPOSITION);
        if (cdf) {
            auto disp = cdf->getValue<vmime::contentDisposition>();
            if (disp && disp->getName() != vmime::contentDispositionTypes::INLINE) {
                isAttachment = true;
                auto cdfField = vmime::dynamicCast<vmime::contentDispositionField>(cdf);
                if (cdfField && cdfField->hasFilename()) {
                    filename = cdfField->getFilename().getBuffer();
                }
            }
        }
    }

    auto mainType = ct.getType();
    auto subType = ct.getSubType();

    if (mainType != vmime::mediaTypes::TEXT && !isAttachment) {
        isAttachment = true;
    }

    if (!isAttachment) return false;

    if (currentIndex == targetIndex) {
        std::string data;
        vmime::utility::outputStreamStringAdapter osa(data);
        body->getContents()->extract(osa);
        osa.flush();
        std::ofstream out(outputPath, std::ios::binary);
        if (!out.is_open()) return false;
        out.write(data.data(), (std::streamsize)data.size());
        out.close();
        return true;
    }

    currentIndex++;
    return false;
}

extern "C" int email_save_attachment(const char* emlPath, int attachmentIndex, const char* outputPath) {
    if (!emlPath || !outputPath || attachmentIndex < 0) return -1;

    try {
        std::ifstream file(emlPath, std::ios::binary);
        if (!file.is_open()) return -2;

        std::ostringstream oss;
        oss << file.rdbuf();
        std::string content = oss.str();
        file.close();

        vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();
        msg->parse(content);

        int currentIndex = 0;
        if (extractAttachmentToFile(msg, attachmentIndex, currentIndex, outputPath)) {
            LOG_INFO("email_save_attachment: saved attachment %d from %s to %s\n",
                     attachmentIndex, emlPath, outputPath);
            return 0;
        }
        return -3;
    } catch (const std::exception& e) {
        LOG_INFO("email_save_attachment: exception: %s\n", e.what());
        return -4;
    } catch (...) {
        return -5;
    }
}

// ---------------------------------------------------------------------------
// Task table operations for queued email sending
// ---------------------------------------------------------------------------

extern "C" int email_task_insert(const char* account, const char* recipient,
                                 const char* subject, const char* body,
                                 const char* in_reply_to, const char* message_id,
                                 const char* x_message_id, const char* session_id,
                                 const char* x_session_chart) {
    if (!account || !recipient || !subject || !body) {
        LOG_INFO("[Task] insert failed: null parameters (account=%p, recipient=%p, subject=%p, body=%p)\n",
                account, recipient, subject, body);
        return -1;
    }
    auto& conn = DbConnection::instance();
    if (!conn.get()) {
        LOG_INFO("[Task] insert failed: database not initialized\n");
        return -2;
    }

    static TaskRepo s_taskRepo;
    int64_t id = s_taskRepo.insert(account, recipient, subject, body,
                                    in_reply_to ? in_reply_to : "",
                                    message_id ? message_id : "",
                                    x_message_id ? x_message_id : "",
                                    session_id ? session_id : "",
                                    x_session_chart ? x_session_chart : XMailer::TEXT);
    if (id == 0) {
        LOG_INFO("[Task] insert failed: id=0 returned from repo\n");
        return -3;
    }
    LOG_INFO("[Task] inserted id=%lld for account=%s\n", (long long)id, account);
    return (int)id;
}

extern "C" int email_task_query_pending(const char* account, char* outJson, int outSize) {
    if (!account || !outJson || outSize <= 0) return -1;
    auto& conn = DbConnection::instance();
    if (!conn.get()) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        return -2;
    }

    static TaskRepo s_taskRepo;
    auto tasks = s_taskRepo.queryPending(account, 10);

    json result = json::array();
    for (const auto& t : tasks) {
        result.push_back({
            {"id", t.id},
            {"account", t.account},
            {"recipient", t.recipient},
            {"subject", t.subject},
            {"body", t.body},
            {"in_reply_to", t.inReplyTo},
            {"message_id", t.messageId},
            {"x_message_id", t.xMessageId},
            {"session_id", t.sessionId},
            {"x_session_chart", t.xSessionChart},
            {"status", t.status},
            {"retry_count", t.retryCount},
            {"next_retry_at", t.nextRetryAt},
            {"last_error", t.lastError},
            {"created_at", t.createdAt}
        });
    }

    std::string out = result.dump();
    if ((int)out.size() >= outSize) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"buffer_too_small"})");
        return -3;
    }
    snprintf(outJson, outSize, "%s", out.c_str());
    return 0;
}

extern "C" int email_task_mark_sent(int taskId) {
    auto& conn = DbConnection::instance();
    if (!conn.get()) return -1;

    static TaskRepo s_taskRepo;
    if (s_taskRepo.markSent(taskId)) {
        LOG_INFO("[Task] marked sent id=%d\n", taskId);
        return 0;
    }
    return -2;
}

extern "C" int email_task_mark_failed(int taskId) {
    auto& conn = DbConnection::instance();
    if (!conn.get()) return -1;

    static TaskRepo s_taskRepo;
    if (s_taskRepo.markFailed(taskId)) {
        LOG_INFO("[Task] marked failed id=%d\n", taskId);
        return 0;
    }
    return -2;
}

extern "C" int email_task_delete(int taskId) {
    auto& conn = DbConnection::instance();
    if (!conn.get()) return -1;

    static TaskRepo s_taskRepo;
    if (s_taskRepo.deleteTask(taskId)) {
        LOG_INFO("[Task] deleted id=%d\n", taskId);
        return 0;
    }
    return -2;
}

extern "C" int email_task_process_pending(int configIndex, const char* account, char* outJson, int outSize) {
    if (!account || !outJson || outSize <= 0) return -1;
    auto& conn = DbConnection::instance();
    if (!conn.get()) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        return -2;
    }

    // Per-account throttle: SMTP providers (163 etc.) limit send frequency per
    // mailbox. Space sends at least MIN_SEND_INTERVAL_SEC apart per account and
    // send at most one task per poll, so the 5s background loop becomes a
    // gentle paced sender instead of a burst that trips rate limiting.
    static std::map<std::string, std::chrono::steady_clock::time_point> s_lastSend;
    static const int MIN_SEND_INTERVAL_SEC = 30;
    {
        auto now = std::chrono::steady_clock::now();
        auto it = s_lastSend.find(account);
        if (it != s_lastSend.end() &&
            std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count() < MIN_SEND_INTERVAL_SEC) {
            snprintf(outJson, outSize, R"({"status":"throttled","sent":0})");
            return 0;
        }
    }

    static TaskRepo s_taskRepo;
    auto tasks = s_taskRepo.queryPending(account, 1);
    if (tasks.empty()) {
        snprintf(outJson, outSize, R"({"status":"success","sent":0})");
        return 0;
    }

    int sentCount = 0;
    json sentTasks = json::array();

    static EmailRepo s_emailRepo;
    static GroupSessionRepo s_groupRepo;

    for (const auto& t : tasks) {
        LOG_INFO("[Task] processing id=%lld for account=%s, message_id='%s'\n", (long long)t.id, account, t.messageId.c_str());

        // Set SMTP server from the Email object
        auto emailObj = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
        if (emailObj) {
            auto delegate = emailObj->get_delegate();
            if (delegate) {
                auto outlookDelegate = std::dynamic_pointer_cast<EmailComm::EmailOptOutlookImpl>(delegate);
                if (outlookDelegate) {
                    outlookDelegate->set_smtp_server(emailObj->get_smtp_address(), emailObj->get_smtp_port());
                }
                auto delegate163 = std::dynamic_pointer_cast<EmailComm::EmailOpt163Impl>(delegate);
                if (delegate163) {
                    delegate163->set_smtp_server(emailObj->get_smtp_address(), emailObj->get_smtp_port());
                }
                auto delegateGmail = std::dynamic_pointer_cast<EmailComm::EmailOptGmailImpl>(delegate);
                if (delegateGmail) {
                    delegateGmail->set_smtp_server(emailObj->get_smtp_address(), emailObj->get_smtp_port());
                }
            }
        }

        // Determine if this task needs Signal encryption
        int encryptMethod = 0;
        if (t.xSessionChart == XMailer::SESSION_INIT ||
            t.xSessionChart == XMailer::RATCHET_MSG) {
            encryptMethod = 1;
        }

        // MLS application messages: encrypt the plaintext body right before sending.
        // The task body holds {x_message_id, x_reply_to, sender, plaintext}; we replace
        // it with the wire body {x_message_id, x_reply_to, sender, ciphertext} and keep
        // the plaintext locally via group_after_sent.
        std::string localBody = t.body;
        std::string wireBody = t.body;
        if (XMailer::isMls(t.xSessionChart)) {
            // File chunk envelopes (~4MB base64) need a body-sized buffer.
            std::vector<char> outBody(t.body.size() * 2 + 65536);
            int prc = group_prepare_outgoing(account, t.xSessionChart.c_str(), t.body.c_str(),
                                             t.inReplyTo.c_str(), outBody.data(), (int)outBody.size());
            if (prc != 0) {
                s_taskRepo.markFailed(t.id);
                LOG_INFO("[Task] MLS prepare_outgoing failed id=%lld rc=%d\n", (long long)t.id, prc);
                continue;
            }
            wireBody = outBody.data();
        }

        // 1:1 file transfer (1.0.4 meta / 1.0.5 chunk): Double-Ratchet-encrypt right before
        // sending so the ratchet advances in send order; t.sessionId is the unified session.
        // The transport gets pre_encrypted=true (send verbatim) and a plaintext local_body.
        bool preEncrypted = false;
        std::string transportSessionId = t.sessionId;
        if (t.xSessionChart == XMailer::ATTACH_META || t.xSessionChart == XMailer::ATTACH_CHUNK) {
            static UnifiedSessionManager s_usMgr;
            std::string envelope, err;
            if (!s_usMgr.signalEncrypt(account, t.sessionId, t.body, t.messageId, t.inReplyTo, envelope, err)) {
                s_taskRepo.markFailed(t.id);
                LOG_INFO("[Task] file chunk encrypt failed id=%lld: %s\n", (long long)t.id, err.c_str());
                continue;
            }
            wireBody = envelope;
            preEncrypted = true;
            encryptMethod = 1;
            transportSessionId = "";  // unified id is not a legacy email session id
            if (t.xSessionChart == XMailer::ATTACH_CHUNK) {
                localBody = "[file chunk]";
            }
        }

        // 1:1 ratchet app message (1.0.2): the body was already Double-Ratchet-
        // encrypted once at enqueue time (us_send_message), so it must be sent
        // verbatim — including on retries, where re-encrypting would advance the
        // ratchet again and desync the peer.
        if (t.xSessionChart == XMailer::RATCHET_MSG && t.preEncrypted) {
            wireBody = t.body;
            localBody = t.localBody;
            preEncrypted = true;
            encryptMethod = 1;
            transportSessionId = "";  // unified id is not a legacy email session id
        }

        // Organize email content JSON
        json emailContent = {
            {"recipient", t.recipient},
            {"subject", t.subject},
            {"body", wireBody},
            {"in_reply_to", t.inReplyTo},
            {"message_id", t.messageId},
            {"x_message_id", t.xMessageId},
            {"session_id", transportSessionId},
            {"x_session_chart", t.xSessionChart},
            {"encrypt_method", encryptMethod}
        };
        if (preEncrypted) {
            emailContent["pre_encrypted"] = true;
            emailContent["local_body"] = localBody;
        }

        // Note: encryption is handled by send_email() when X-Mailer is 1.0.2/1.0.4/1.0.5
        // Do NOT encrypt here - it would cause double encryption

        std::string emailStr = emailContent.dump();
        int sendRc = SendEmail_c(configIndex, emailStr.c_str());
        // A send attempt counts against the account throttle whether it succeeded
        // or failed: hammering a rate-limited server only extends the cooldown.
        s_lastSend[account] = std::chrono::steady_clock::now();
        if (sendRc == 0) {
            s_taskRepo.deleteTask(t.id);
            sentCount++;
            sentTasks.push_back({{"id", t.id}, {"message_id", t.messageId}});
            LOG_INFO("[Task] sent successfully and deleted id=%lld\n", (long long)t.id);

            if (XMailer::isMls(t.xSessionChart)) {
                // Resolve group + (for app messages) store the plaintext in the local .eml
                std::string dataDir;
                auto emailObj = oemailim::EmailHandler::g_EmailConfigIndices[configIndex];
                if (emailObj) {
                    auto delegate = emailObj->get_delegate();
                    if (delegate) {
                        auto d163 = std::dynamic_pointer_cast<EmailComm::EmailOpt163Impl>(delegate);
                        if (d163) dataDir = d163->get_data_dir();
                        auto dOutlook = std::dynamic_pointer_cast<EmailComm::EmailOptOutlookImpl>(delegate);
                        if (dOutlook) dataDir = dOutlook->get_data_dir();
                        auto dGmail = std::dynamic_pointer_cast<EmailComm::EmailOptGmailImpl>(delegate);
                        if (dGmail) dataDir = dGmail->get_data_dir();
                    }
                }
                group_after_sent(account, t.xSessionChart.c_str(), t.messageId.c_str(), t.inReplyTo.c_str(),
                                 localBody.c_str(), dataDir.c_str());
            }
        } else {
            // Classify the failure. Content that can never be sent succeeds at
            // nothing, so fail it permanently; network errors and SMTP throttling
            // (451/452, frequency limits, timeouts) retry with exponential backoff.
            std::string errText;
            if (emailObj) {
                auto delegate = emailObj->get_delegate();
                if (delegate) errText = delegate->get_last_error();
            }
            // Permanent: the content or local crypto context can never succeed —
            // retrying just burns the account's send budget. Everything else
            // (network, SMTP throttling 451/452, timeouts) is retryable.
            bool permanent = errText.find("JSON parse error") != std::string::npos ||
                             errText.find("no recipient") != std::string::npos ||
                             errText.find("Bad address") != std::string::npos ||
                             errText.find("No active Signal session") != std::string::npos ||
                             errText.find("no auth code") != std::string::npos;
            if (permanent) {
                s_taskRepo.markFailed(t.id, errText);
                LOG_INFO("[Task] send permanently failed id=%lld, rc=%d: %s\n",
                         (long long)t.id, sendRc, errText.c_str());
            } else {
                int backoffSec = 60 << std::min(t.retryCount, 4);  // 60,120,240,480,960
                s_taskRepo.markRetryable(t.id, backoffSec, errText);
                LOG_INFO("[Task] send retryable id=%lld, rc=%d, backoff=%ds: %s\n",
                         (long long)t.id, sendRc, backoffSec, errText.c_str());
            }
        }
    }

    json result = {
        {"status", "success"},
        {"sent", sentCount},
        {"tasks", sentTasks}
    };
    std::string out = result.dump();
    LOG_INFO("[Task] returning result: %s\n", out.c_str());
    if ((int)out.size() >= outSize) {
        snprintf(outJson, outSize, R"({"status":"success","sent":%d})", sentCount);
        return 0;
    }
    snprintf(outJson, outSize, "%s", out.c_str());
    return 0;
}

// Query outbox task status by account + message_id (for GUI "sending..." state).
extern "C" int email_task_status(const char* account, const char* messageId, char* outJson, int outSize) {
    if (!account || !messageId || !outJson || outSize <= 0) return -1;
    auto& conn = DbConnection::instance();
    if (!conn.get()) {
        snprintf(outJson, outSize, R"({"status":"failed","error":"database_not_initialized"})");
        return -2;
    }

    static TaskRepo s_taskRepo;
    TaskRecord rec;
    if (!s_taskRepo.queryByMessageId(account, messageId, rec)) {
        snprintf(outJson, outSize, R"({"status":"success","task_status":"none"})");
        return 0;
    }

    json resp;
    resp["status"] = "success";
    resp["task_status"] = (rec.status == 2) ? "failed" : "pending";
    resp["retry_count"] = rec.retryCount;
    resp["next_retry_at"] = rec.nextRetryAt;
    resp["last_error"] = rec.lastError;
    std::string out = resp.dump();
    snprintf(outJson, outSize, "%s", out.c_str());
    return 0;
}

// Migration: Update islocal for existing emails
extern "C" int email_migrate_islocal() {
    auto& conn = DbConnection::instance();
    if (!conn.get()) return -1;

    static EmailRepo s_emailRepo;
    if (s_emailRepo.migrateIslocalForNoSessionChart()) {
        LOG_INFO("[Migration] islocal migration completed\n");
        return 0;
    }
    return -2;
}
