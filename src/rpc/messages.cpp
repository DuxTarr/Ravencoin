// Copyright (c) 2017 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assets/assets.h"
#include "assets/assetdb.h"
#include "assets/messages.h"
#include "assets/messagedb.h"
#include <map>
#include <assets/ipfs.h>
#include "tinyformat.h"

#include "amount.h"
#include "base58.h"
#include "chain.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "httpserver.h"
#include "validation.h"
#include "net.h"
#include "policy/feerate.h"
#include "policy/fees.h"
#include "policy/policy.h"
#include "policy/rbf.h"
#include "rpc/mining.h"
#include "rpc/safemode.h"
#include "rpc/server.h"
#include "script/sign.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet/coincontrol.h"
#include "wallet/feebumper.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"

std::string MessageActivationWarning()
{
    return AreMessagingDeployed() ? "" : "\nTHIS COMMAND IS NOT YET ACTIVE!\nhttps://github.com/RavenProject/rips/blob/master/rip-0005.mediawiki\n";
}

UniValue viewallmessages(const JSONRPCRequest& request) {
    if (request.fHelp || !AreMessagingDeployed() || request.params.size() != 0)
        throw std::runtime_error(
                "viewallmessages \n"
                + MessageActivationWarning() +
                "\nView all Messages that the wallet contains\n"

                "\nResult:\n"
                "\"Asset Name:\"                     (string) The name of the asset the message was sent on\n"
                "\"Message:\"                        (string) The IPFS hash that is the message\n"
                "\"Time:\"                           (Date) The time as a date in the format (YY-mm-dd Hour-minute-second)\n"
                "\"Block Height:\"                   (number) The height of the block the message was included in\n"
                "\"Status:\"                         (string) Status of the message (READ, UNREAD, ORPHAN, EXPIRED, SPAM, HIDDEN, ERROR)\n"
                "\"Expire Time:\"                    (Date, optional) If the message had an expiration date assigned, it will be shown hear in the format (YY-mm-dd Hour-minute-second)\n"

                "\nExamples:\n"
                + HelpExampleCli("viewallmessages", "")
                + HelpExampleRpc("viewallmessages", "")
        );

    if (!fMessaging) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Messaging is disabled. To enable messaging, run the wallet without -disablemessaging or remove disablemessaging from your raven.conf");
        return ret;
    }

    if (!pMessagesCache || !pmessagedb) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Messaging database and cache are having problems (a wallet restart might fix this issue)");
        return ret;
    }

    std::set<CMessage> setMessages;

    pmessagedb->LoadMessages(setMessages);

    for (auto pair : mapDirtyMessagesOrphaned) {
        CMessage message = pair.second;
        message.status = MessageStatus::ORPHAN;
        if (setMessages.count(message))
            setMessages.erase(message);
        setMessages.insert(message);
    }

    for (auto out : setDirtyMessagesRemove) {
        CMessage message;
        message.out = out;
        setMessages.erase(message);
    }

    for (auto pair : mapDirtyMessagesAdd) {
        setMessages.erase(pair.second);
        setMessages.insert(pair.second);
    }

    UniValue messages(UniValue::VARR);

    for (auto message : setMessages) {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("Asset Name", message.strName));
        obj.push_back(Pair("Message", EncodeIPFS(message.ipfsHash)));
        obj.push_back(Pair("Time", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", message.time)));
        obj.push_back(Pair("Block Height", message.nBlockHeight));
        obj.push_back(Pair("Status", MessageStatusToString(message.status)));
        if (message.nExpiredTime)
            obj.push_back(Pair("Expire Time", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", message.nExpiredTime)));

        messages.push_back(obj);
    }

    return messages;
}

UniValue viewallmessagechannels(const JSONRPCRequest& request) {
    if (request.fHelp || !AreMessagingDeployed() || request.params.size() != 0)
        throw std::runtime_error(
                "viewallmessagechannels \n"
                + MessageActivationWarning() +
                "\nView all Message Channel the wallet is subscribed to\n"

                "\nResult:[\n"
                "\"Asset Name:\"                     (string) The asset channel name\n"
                "\n]\n"
                "\nExamples:\n"
                + HelpExampleCli("viewallmessagechannels", "")
                + HelpExampleRpc("viewallmessagechannels", "")
        );

    if (!fMessaging) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Messaging is disabled. To enable messaging, run the wallet without -disablemessaging or remove disablemessaging from your raven.conf");
        return ret;
    }

    if (!pMessageSubscribedChannelsCache || !pmessagechanneldb) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Messaging channel database and cache are having problems (a wallet restart might fix this issue)");
        return ret;
    }

    std::set<std::string> setChannels;

    pmessagechanneldb->LoadMyMessageChannels(setChannels);

    LogPrintf("%s: Checking caches removeSize:%u, addSize:%u\n", __func__, setDirtyChannelsRemove.size(), setDirtyChannelsAdd.size());

    for (auto name : setDirtyChannelsRemove) {
        setChannels.erase(name);
    }

    for (auto name : setDirtyChannelsAdd) {
        setChannels.insert(name);
    }

    UniValue channels(UniValue::VARR);

    for (auto name : setChannels) {
        channels.push_back(name);
    }

    return channels;
}

UniValue subscribetochannel(const JSONRPCRequest& request) {
    if (request.fHelp || !AreMessagingDeployed() || request.params.size() != 1)
        throw std::runtime_error(
                "subscribetochannel \n"
                + MessageActivationWarning() +
                "\nSubscribe to a certain messagechannel\n"

                "\nArguments:\n"
                "1. \"channel_name\"            (string, required) The channel name to subscribe to, it must end with '!' or have an '~' in the name\n"

                "\nResult:[\n"
                "\n]\n"
                "\nExamples:\n"
                + HelpExampleCli("subscribetochannel", "\"ASSET_NAME!\"")
                + HelpExampleRpc("subscribetochannel", "\"ASSET_NAME!\"")
        );

    if (!fMessaging) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Messaging is disabled. To enable messaging, run the wallet without -disablemessaging or remove disablemessaging from your raven.conf");
    }

    if (!pMessageSubscribedChannelsCache || !pmessagechanneldb) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Message database isn't setup");
    }

    std::string channel_name = request.params[0].get_str();

    AssetType type;
    if (!IsAssetNameValid(channel_name, type))
        throw JSONRPCError(
                RPC_INVALID_PARAMETER, "Channel Name is not valid.");

    if (type != AssetType::OWNER && type != AssetType::MSGCHANNEL)
        throw JSONRPCError(
                RPC_INVALID_PARAMETER, "Channel Name must must a owner asset, or a message channel asset e.g OWNER!, MSG_CHANNEL~123.");

    AddChannel(channel_name);

    return NullUniValue;
}


UniValue unsubscribefromchannel(const JSONRPCRequest& request) {
    if (request.fHelp || !AreMessagingDeployed() || request.params.size() != 1)
        throw std::runtime_error(
                "unsubscribefromchannel \n"
                + MessageActivationWarning() +
                "\nUnsubscribe from a certain messagechannel\n"

                "\nArguments:\n"
                "1. \"channel_name\"            (string, required) The channel name to unscribe from, must end with '!' or have an '~' in the name\n"

                "\nResult:[\n"
                "\n]\n"
                "\nExamples:\n"
                + HelpExampleCli("unsubscribefromchannel", "\"ASSET_NAME!\"")
                + HelpExampleRpc("unsubscribefromchannel", "\"ASSET_NAME!\"")
        );

    if (!fMessaging) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Messaging is disabled. To enable messaging, run the wallet without -disablemessaging or remove disablemessaging from your raven.conf");
    }

    if (!pMessageSubscribedChannelsCache || !pmessagechanneldb) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Message database isn't setup");
    }

    std::string channel_name = request.params[0].get_str();

    AssetType type;
    if (!IsAssetNameValid(channel_name, type))
        throw JSONRPCError(
                RPC_INVALID_PARAMETER, "Channel Name is not valid.");

    if (type != AssetType::OWNER && type != AssetType::MSGCHANNEL)
        throw JSONRPCError(
                RPC_INVALID_PARAMETER, "Channel Name must must a owner asset, or a message channel asset e.g OWNER!, MSG_CHANNEL~123.");

    RemoveChannel(channel_name);

    return NullUniValue;
}

UniValue startipfsnode(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
                "startipfsnode \n"
                "\nStart the ipfs local node startup sequences\n"


                "\nResult:[\n"
                "\n]\n"
                "\nExamples:\n"
                + HelpExampleCli("startipfsnode", "")
                + HelpExampleRpc("startipfsnode", "")
        );


    StartUpLocalIpfsNode();

    return NullUniValue;
}

UniValue stopipfsnode(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
                "stopipfsnode \n"
                "\nStop the ipfs local node daemon\n"


                "\nResult:[\n"
                "\n]\n"
                "\nExamples:\n"
                + HelpExampleCli("stopipfsnode", "")
                + HelpExampleRpc("stopipfsnode", "")
        );

    StopLocalIpfsNode();

    return NullUniValue;
}



UniValue getipfsnodestatus(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
                "getipfsnodestatus \n"
                "\nGet the status of the local ipfs node\n"


                "\nResult: status\n"
                "\n\n"
                "\nExamples:\n"
                + HelpExampleCli("getipfsnodestatus", "")
                + HelpExampleRpc("getipfsnodestatus", "")
        );


    // Lock the ipfs lock and check that state
    {
        LOCK(cs_ipfs);
        return IPFSStateToString(globalIpfsState);
    }
}

UniValue addipfsdata(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
                "addipfsdata \n"
                "\nAdd data and get the ipfs hash associated with it\n"

                "\nArguments:\n"
                "1. \"data\"            (string, required) The data that you want to make an ipfs hash with\n"

                "\nResult: ipfs hash (string)\n"
                "\n\n"
                "\nExamples:\n"
                + HelpExampleCli("addipfsdata", "")
                + HelpExampleRpc("addipfsdata", "")
        );

    if (!fIPFS) // Stop if ipfs is turned off on the client
        throw JSONRPCError(RPC_INVALID_REQUEST, _("Ipfs operations have been turned off on this client. Remove -noipfs from your raven.conf or command line call"));

    // Lock the ipfs lock and check that state
    {
        LOCK(cs_ipfs);
        if (globalIpfsState != IPFS_STATE::DAEMON_CLIENT_CONNECTED) {
            throw JSONRPCError(RPC_INVALID_REQUEST, _("Local Ipfs node is not running, to run it you must have ipfs install, and then use the command startipfsnode"));
        }
    }

    // Get the data from the rpc call
    std::string str = request.params[0].get_str();

    // Check the data against the max data size
    if (str.size() > MAX_IPFS_DATA_SIZE)
        throw JSONRPCError(RPC_INVALID_REQUEST, _("Data to large to add via ravencoin. All data added is also pinned to the local ipfs node. This will help save storage."));

    // Add the ipfsdata to the ipfs network
    AddResult result;
    if (!AddIpfsData(str, result)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, _("Failed to add data to ipfs"));
    } else {
        return result.ipfsHash;
    }
}

UniValue pinipfshash(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
                "pinipfshash \n"
                "\nAdd data and get the ipfs hash associated with it\n"

                "\nArguments:\n"
                "1. \"ipfs_id\"            (string, required) The id (hash) of the ipfs data you want to pin\n"

                "\nResult: \n"
                "\n\n"
                "\nExamples:\n"
                + HelpExampleCli("pinipfshash", "")
                + HelpExampleRpc("pinipfshash", "")
        );

    if (!fIPFS) // Stop if ipfs is turned off on the client
        throw JSONRPCError(RPC_INVALID_REQUEST, _("Ipfs operations have been turned off on this client. Remove -noipfs from your raven.conf or command line call"));

    // Lock the ipfs lock and check that state
    {
        LOCK(cs_ipfs);
        if (globalIpfsState != IPFS_STATE::DAEMON_CLIENT_CONNECTED) {
            throw JSONRPCError(RPC_INVALID_REQUEST, _("Local Ipfs node is not running, to run it you must have ipfs install, and then use the command startipfsnode"));
        }
    }

    // Get the data from the rpc call
    std::string str = request.params[0].get_str();
    std::string error;
    if (!CheckIPFSHash(str, error)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, error);
    }
    // Pin the ipfs_id
    if (!PinIpfsFile(str))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to pin the file to ipfs");
    return NullUniValue;
}

static const CRPCCommand commands[] =
    {           //  category    name                          actor (function)             argNames
                //  ----------- ------------------------      -----------------------      ----------
            { "messages",   "viewallmessages",                &viewallmessages,            {}},
            { "messages",   "viewallmessagechannels",         &viewallmessagechannels,     {}},
            { "messages",   "subscribetochannel",             &subscribetochannel,         {"channel_name"}},
            { "messages",   "unsubscribefromchannel",         &unsubscribefromchannel,     {"channel_name"}},
            { "messages",   "getipfsnodestatus",              &getipfsnodestatus,          {}},
            { "messages",   "startipfsnode",                  &startipfsnode,              {}},
            { "messages",   "stopipfsnode",                   &stopipfsnode,               {}},
            { "messages",   "addipfsdata",                    &addipfsdata,                {"data"}},
            { "messages",   "pinipfshash",                    &pinipfshash,                {"ipfs_id"}}
    };

void RegisterMessageRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
