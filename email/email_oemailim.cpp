#include "email_core_common.h"
#include "email_core.h"
#include "logger.h"
#include "email_handler_c.h"
#include "email_handler.h"
#include "email_opt_163_impl.h"
#include "email_opt_outlook_impl.h"
#include "email_opt_gmail_impl.h"
#include "db_connection.h"
#include "email_repo.h"
#include "session_repo.h"
#include "key_repo.h"
#include "task_repo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <map>
#include <vector>

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
extern "C" int email_create_session(const char* account, const char* subject, const char* members, const char* message_id, int encrypt_method, char* outJson, int outSize);

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

    auto pendingRecs = s_emailRepo.queryPendingEmails(accountStr, 10);

    struct PendingEmail {
        std::string uuid;
        std::string folder;
    };
    std::vector<PendingEmail> pending;
    for (const auto& pr : pendingRecs) {
        pending.push_back({pr.uuid, pr.folder});
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

    for (const auto& pe : pending) {
        std::string filePath = accountDir + "/" + pe.uuid + ".eml";

        int getResult = GetEmailToFile_c(configIndex, pe.folder.c_str(), pe.uuid.c_str(), filePath.c_str());
        if (getResult != 0) {
            LOG_INFO("[DB] download_pending: failed to fetch uid=%s folder=%s: %d\n", pe.uuid.c_str(), pe.folder.c_str(), getResult);
            if (getResult != -10) {
                s_emailRepo.incrementRetryCount(pe.uuid, accountStr);
            }
            continue;
        }

        std::string message_id = "";
        std::string in_reply_to = "";
        try {
            std::ifstream emlFile(filePath);
            std::string emlContent((std::istreambuf_iterator<char>(emlFile)),
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

            // Check X-Message-ID (priority over Message-ID) and X-Session-Chart
            std::string x_message_id = decodeHeader(getHeader("X-Message-ID"));
            std::string x_session_chart = decodeHeader(getHeader("X-Session-Chart"));
            std::string eml_subject = decodeHeader(getHeader("Subject"));
            std::string eml_to = decodeHeader(getHeader("To"));
            std::string eml_from = decodeHeader(getHeader("From"));
            if (!x_message_id.empty()) {
                message_id = x_message_id;
            }

            LOG_INFO("[DB] download_pending: parsed message_id='%s', in_reply_to='%s', x_session_chart='%s' from %s\n",
                     message_id.c_str(), in_reply_to.c_str(), x_session_chart.c_str(), filePath.c_str());

            // If X-Session-Chart=new, parse body as JSON to extract session_info
            if (x_session_chart == "new") {
                LOG_INFO("[DB] download_pending: X-Session-Chart=new, parsing body for session_info\n");
                try {
                    // Parse the full EML to extract body text
                    vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();
                    msg->parse(emlContent);
                    std::string bodyText;
                    // Use extractParts helper to get text body
                    std::string textBody, htmlBody;
                    json dummyAttachments = json::array();
                    bool dummyHasAttachment = false;
                    extractParts(std::static_pointer_cast<vmime::bodyPart>(msg), textBody, htmlBody, dummyAttachments, dummyHasAttachment);
                    bodyText = textBody.empty() ? htmlBody : textBody;

                    // Parse body as JSON: { "text": "...", "session_info": { "title": "...", "account": "...", "decodetype": 1, "needkey": [...] } }
                    auto bodyJson = json::parse(bodyText);
                    std::string text = bodyJson.value("text", "");
                    auto sessionInfo = bodyJson.value("session_info", json::object());
                    std::string siTitle = sessionInfo.value("title", "");
                    std::string siAccount = sessionInfo.value("account", "");
                    int decodeType = sessionInfo.value("decodetype", 0);
                    std::vector<std::string> needkeyList;
                    if (sessionInfo.contains("needkey") && sessionInfo["needkey"].is_array()) {
                        for (const auto& nk : sessionInfo["needkey"]) {
                            needkeyList.push_back(nk.get<std::string>());
                        }
                    }

                    LOG_INFO("[DB] download_pending: session_info title=%s, account=%s, decodetype=%d, needkey_count=%zu\n",
                             siTitle.c_str(), siAccount.c_str(), decodeType, needkeyList.size());

                    // Create a new session on the receiver side
                    // But first check if a session already exists for this message_id
                    // (e.g. sender already created the session when sending)
                    std::string existingSessionId;
                    {
                        char existingSid[512];
                        existingSid[0] = '\0';
                        email_query_session_by_message_id(message_id.c_str(), accountStr.c_str(), existingSid, sizeof(existingSid));
                        existingSessionId = existingSid;
                    }

                    std::string newSessionId;
                    if (!existingSessionId.empty()) {
                        // Session already exists, reuse it
                        newSessionId = existingSessionId;
                        LOG_INFO("[DB] download_pending: reusing existing session_id=%s for message_id=%s\n",
                                 newSessionId.c_str(), message_id.c_str());
                    } else {
                        // No existing session, create a new one
                        char create_session_json[4096];
                        int create_rc = email_create_session(
                            accountStr.c_str(),
                            siTitle.empty() ? eml_subject.c_str() : siTitle.c_str(),
                            siAccount.c_str(),
                            message_id.c_str(),
                            decodeType,
                            create_session_json,
                            sizeof(create_session_json)
                        );
                        LOG_INFO("[DB] download_pending: email_create_session result=%d, json=%s\n", create_rc, create_session_json);

                        // Get the new session_id from the response
                        if (create_rc == 0) {
                            try {
                                auto createResp = json::parse(create_session_json);
                                if (createResp.value("status", "") == "success") {
                                    newSessionId = createResp.value("session_id", "");
                                }
                            } catch (...) {}
                        }
                    }

                    // Associate this email with the session
                    if (!newSessionId.empty()) {
                        // Find localemail id from uuid
                        int64_t emailId = s_emailRepo.findRowidByUuidAndAccount(pe.uuid, accountStr);
                        if (emailId > 0) {
                                std::string emailIdStr = std::to_string(emailId);
                                char session_result_json[4096];
                                int add_rc = email_add_email_to_session(
                                    newSessionId.c_str(),
                                    emailIdStr.c_str(),
                                    accountStr.c_str(),
                                    decodeType,
                                    session_result_json,
                                    sizeof(session_result_json)
                                );
                                LOG_INFO("[DB] download_pending: email_add_email_to_session session_id=%s, email_id=%s, result=%d\n",
                                         newSessionId.c_str(), emailIdStr.c_str(), add_rc);
                        }
                    }

                    // Only process key exchange for standard encryption (decodetype==1)
                    if (decodeType == 1) {
                        // Check if self (accountStr) is in needkey list
                        bool selfInNeedKey = false;
                        for (const auto& nk : needkeyList) {
                            if (nk == accountStr) {
                                selfInNeedKey = true;
                                break;
                            }
                        }

                        if (selfInNeedKey) {
                        LOG_INFO("[DB] download_pending: self (%s) is in needkey, sending exchange email\n", accountStr.c_str());

                        // Check if we already have our own keypair in keyinfo table (primary source)
                        std::string myPubkey, myPrivPem, myKeyPassword;
                        myPubkey = s_keyRepo.queryLatestPubkeyFromKeyInfo(accountStr);

                        // Fallback: check code table
                        if (myPubkey.empty()) {
                            char codeQueryJson[8192];
                            email_code_query_by_account(accountStr.c_str(), codeQueryJson, sizeof(codeQueryJson));
                            try {
                                auto codeResults = json::parse(codeQueryJson);
                                if (codeResults.is_array() && !codeResults.empty()) {
                                    myPubkey = codeResults[0].value("pubkey", "");
                                }
                            } catch (...) {}
                        }

                        // If still no pubkey, generate a new keypair and store in both keyinfo and code
                        if (myPubkey.empty()) {
                            myKeyPassword = generate_random_password(32);
                            std::string genPub, genPriv;
                            if (generate_ecc_keypair(genPub, genPriv, myKeyPassword)) {
                                myPubkey = genPub;
                                myPrivPem = genPriv;
                                email_code_insert(accountStr.c_str(), myPubkey.c_str(), myPrivPem.c_str(), myKeyPassword.c_str());
                                s_keyRepo.insertKeyInfo(genPub, genPriv, myKeyPassword, 0, accountStr);
                                LOG_INFO("[DB] download_pending: generated new keypair for self, pubkey_len=%zu\n", myPubkey.size());
                            } else {
                                LOG_INFO("[DB] download_pending: failed to generate keypair for self\n");
                            }
                        }

                        // Send exchange email to all members from needkey list (including self)
                        if (!myPubkey.empty()) {
                            // Build recipient string from needkey list
                            std::string recipientStr;
                            for (size_t i = 0; i < needkeyList.size(); i++) {
                                if (i > 0) recipientStr += ", ";
                                recipientStr += needkeyList[i];
                            }

                            LOG_INFO("[DB] download_pending: exchange email recipients: %s\n", recipientStr.c_str());

                            json exchangeBody;
                            exchangeBody["text"] = "密钥交换";
                            exchangeBody["session_info"] = {
                                {"account", accountStr},
                                {"pubkey", myPubkey}
                            };

                            json exchangeContent;
                            exchangeContent["recipient"] = recipientStr;
                            exchangeContent["subject"] = eml_subject;
                            exchangeContent["body"] = exchangeBody.dump();
                            exchangeContent["in_reply_to"] = message_id;
                            exchangeContent["message_id"] = "";
                            exchangeContent["session_id"] = "";
                            exchangeContent["x_message_id"] = "";
                            exchangeContent["x_session_chart"] = "exchange";

                            std::string exchangeStr = exchangeContent.dump();
                            int sendRc = SendEmail_c(configIndex, exchangeStr.c_str());
                            LOG_INFO("[DB] download_pending: exchange email sent to %s, result=%d\n", recipientStr.c_str(), sendRc);
                        }
                    }
                    } // end if (decodeType == 1)
                } catch (const std::exception& e) {
                    LOG_INFO("[DB] download_pending: failed to parse session_info from body: %s\n", e.what());
                }
            }

            // If X-Session-Chart=exchange, save received pubkey to code table
            if (x_session_chart == "exchange") {
                LOG_INFO("[DB] download_pending: X-Session-Chart=exchange, parsing body for session_info\n");
                try {
                    vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();
                    msg->parse(emlContent);
                    std::string bodyText;
                    std::string textBody, htmlBody;
                    json dummyAttachments = json::array();
                    bool dummyHasAttachment = false;
                    extractParts(std::static_pointer_cast<vmime::bodyPart>(msg), textBody, htmlBody, dummyAttachments, dummyHasAttachment);
                    bodyText = textBody.empty() ? htmlBody : textBody;

                    auto bodyJson = json::parse(bodyText);
                    auto sessionInfo = bodyJson.value("session_info", json::object());
                    std::string siAccount = sessionInfo.value("account", "");
                    std::string siPubkey = sessionInfo.value("pubkey", "");

                    LOG_INFO("[DB] download_pending: exchange from account=%s, pubkey_len=%zu\n",
                             siAccount.c_str(), siPubkey.size());

                    if (!siAccount.empty() && !siPubkey.empty()) {
                        int codeRc = email_code_insert(siAccount.c_str(), siPubkey.c_str(), "", "");
                        LOG_INFO("[DB] download_pending: exchange email_code_insert result=%d\n", codeRc);
                    }

                    // Add this received exchange email to the session (find via in_reply_to)
                    if (!in_reply_to.empty()) {
                        char existingSid[512];
                        existingSid[0] = '\0';
                        email_query_session_by_message_id(in_reply_to.c_str(), accountStr.c_str(), existingSid, sizeof(existingSid));
                        std::string foundSid = existingSid;
                        if (foundSid.empty() && !message_id.empty()) {
                            email_query_session_by_message_id(message_id.c_str(), accountStr.c_str(), existingSid, sizeof(existingSid));
                            foundSid = existingSid;
                        }
                        if (!foundSid.empty()) {
                            int64_t emailId = s_emailRepo.findRowidByUuidAndAccount(pe.uuid, accountStr);
                            if (emailId > 0) {
                                    std::string emailIdStr = std::to_string(emailId);
                                    char session_result_json[4096];
                                    int add_rc = email_add_email_to_session(
                                        foundSid.c_str(),
                                        emailIdStr.c_str(),
                                        accountStr.c_str(),
                                        0,
                                        session_result_json,
                                        sizeof(session_result_json)
                                    );
                                    LOG_INFO("[DB] download_pending: exchange email added to session=%s, email_id=%s, result=%d\n",
                                             foundSid.c_str(), emailIdStr.c_str(), add_rc);
                            }
                        } else {
                            LOG_INFO("[DB] download_pending: exchange email - could not find session for in_reply_to=%s\n", in_reply_to.c_str());
                        }
                    }
                } catch (const std::exception& e) {
                    LOG_INFO("[DB] download_pending: failed to parse exchange session_info: %s\n", e.what());
                }
            }

            // If X-Session-Chart=data, decrypt the body
            if (x_session_chart == "data") {
                LOG_INFO("[DB] download_pending: X-Session-Chart=data, decrypting body\n");
                try {
                    vmime::shared_ptr<vmime::message> msg = vmime::make_shared<vmime::message>();
                    msg->parse(emlContent);
                    std::string textBody, htmlBody;
                    json dummyAttachments = json::array();
                    bool dummyHasAttachment = false;
                    extractParts(std::static_pointer_cast<vmime::bodyPart>(msg), textBody, htmlBody, dummyAttachments, dummyHasAttachment);
                    std::string bodyText = textBody.empty() ? htmlBody : textBody;

                    char decrypted[65536];
                    int decRc = email_decrypt_data_body(bodyText.c_str(), accountStr.c_str(), decrypted, sizeof(decrypted));
                    if (decRc == 0) {
                        LOG_INFO("[DB] download_pending: data decrypted, plaintext_len=%zu\n", strlen(decrypted));
                        // Replace the eml content with decrypted plaintext
                        // Rebuild a simple text/plain email with the decrypted content
                        emlContent.clear();
                        emlContent += "Content-Type: text/plain; charset=utf-8\r\n";
                        emlContent += "Content-Transfer-Encoding: 8bit\r\n";
                        emlContent += "\r\n";
                        emlContent += decrypted;
                    } else {
                        LOG_INFO("[DB] download_pending: data decryption failed, rc=%d\n", decRc);
                    }
                } catch (const std::exception& e) {
                    LOG_INFO("[DB] download_pending: failed to decrypt data body: %s\n", e.what());
                }
            }
        } catch (const std::exception& e) {
            LOG_INFO("[DB] download_pending: failed to parse .eml file: %s\n", e.what());
        }

        s_emailRepo.updateAfterDownload(pe.uuid, accountStr, message_id, in_reply_to, pe.uuid);

        downloaded++;
        results.push_back({{"uuid", pe.uuid}, {"folder", pe.folder}, {"file", filePath}});
        LOG_INFO("[DB] download_pending: saved uid=%s to %s\n", pe.uuid.c_str(), filePath.c_str());
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
                                    x_session_chart ? x_session_chart : "data");
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

    static TaskRepo s_taskRepo;
    auto tasks = s_taskRepo.queryPending(account, 10);
    if (tasks.empty()) {
        snprintf(outJson, outSize, R"({"status":"success","sent":0})");
        return 0;
    }

    int sentCount = 0;
    json sentTasks = json::array();

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

        // Organize email content JSON
        json emailContent = {
            {"recipient", t.recipient},
            {"subject", t.subject},
            {"body", t.body},
            {"in_reply_to", t.inReplyTo},
            {"message_id", t.messageId},
            {"x_message_id", t.xMessageId},
            {"session_id", t.sessionId},
            {"x_session_chart", t.xSessionChart}
        };

        // Note: encryption is handled by send_email() when x_session_chart=="data"
        // Do NOT encrypt here - it would cause double encryption

        std::string emailStr = emailContent.dump();
        int sendRc = SendEmail_c(configIndex, emailStr.c_str());
        if (sendRc == 0) {
            s_taskRepo.deleteTask(t.id);
            sentCount++;
            sentTasks.push_back({{"id", t.id}, {"message_id", t.messageId}});
            LOG_INFO("[Task] sent successfully and deleted id=%lld\n", (long long)t.id);
        } else {
            s_taskRepo.markFailed(t.id);
            LOG_INFO("[Task] send failed id=%lld, rc=%d\n", (long long)t.id, sendRc);
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
