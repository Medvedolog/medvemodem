'use strict';

// Telegram application errors are not network failures: never demote a route when the server has answered.
function classify(reply, profile) {
    let data = reply && type(reply.data) == 'object' ? reply.data : {};
    if (!reply?.error && data.ok == true) {
        // 'call': editMessageText, answerCallbackQuery, setMyCommands — their result is not a new message
        if (profile == 'send' && (type(data.result) != 'object' || type(data.result.message_id) != 'int'))
            return {state:'malformed_ack',demote:false,uncertain:true,delay:10};
        if (profile == 'poll' && type(data.result) != 'array')
            return {state:'malformed_poll',demote:false,uncertain:false,delay:30};
        return {state:'ok', demote:false, uncertain:false, delay:0};
    }
    let code = int(data.error_code || 0);
    if (code == 401) return {state:'bad_token',demote:false,uncertain:false,delay:3600};
    if (code == 403) return {state:'forbidden',demote:false,uncertain:false,delay:3600};
    if (code == 409) return {state:'poll_conflict',demote:false,uncertain:false,delay:15};
    if (code == 429) {
        let p = data.parameters || {};
        let wait = int(p.retry_after || 60);
        if (wait < 1) wait = 1;
        if (wait > 900) wait = 900;
        return {state:'rate_limited',demote:false,uncertain:false,delay:wait};
    }
    if (code >= 400 && code < 500)
        return {state:'telegram_rejected',demote:false,uncertain:false,delay:300};
    let err = reply?.error || 'invalid_response';
    // the router's own bundle: no other route can do better
    if (err == 'ca_bundle_missing' || err == 'curl_exit_77')
        return {state:'tls_untrusted',demote:false,uncertain:false,delay:60};
    // A handshake reset (35) or a forged certificate (60, 51) is what a blocked path looks like (DPI): this route is
    // dead, the next one may pass. Verification itself stays strict on every route.
    if (err == 'curl_exit_60' || err == 'curl_exit_51')
        return {state:'tls_untrusted',demote:true,uncertain:false,delay:10};
    if (err == 'curl_exit_35' || err == 'curl_exit_58' || err == 'curl_exit_59')
        return {state:'tls_failure',demote:true,uncertain:false,delay:10};
    if (err == 'curl_exit_6' || err == 'curl_exit_7')
        return {state:'unreachable',demote:true,uncertain:false,delay:5};
    if (code >= 500) return {state:'telegram_error',demote:false,uncertain:false,delay:30};
    // A timed-out sendMessage could already have been accepted by Telegram.
    return {state:'network',demote:true,uncertain:profile == 'send',delay:10};
}

function routeHealthy(follower, index, now, ttl) {
    let t = follower && follower[index];
    return type(t) == 'int' && t > 0 && now - t >= 0 && now - t <= ttl;
}

// ucode as pinned by OpenWrt 24.10 and 25.12 does not parse `export function` declarations
export { classify, routeHealthy };
