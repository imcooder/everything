// Shared commit-message validation for the local commit-msg hook and CI's commit-lint job.
// Keep the two callers using the exact same rule set (checkMessage) so a message that passes
// locally can never fail on GitHub, or vice versa.

// Anything outside this block is rejected as "must be English" (catches CJK and most other
// non-Latin scripts). Deliberately permissive of common ASCII punctuation/typography used
// throughout this repo's existing history (em dash, curly quotes, etc.) rather than requiring
// strict 7-bit ASCII, which would also reject those.
const ALLOWED_CHAR_RANGES = [
    [0x0000, 0x024f], // Basic Latin + Latin-1 Supplement + Latin Extended-A/B
    [0x2010, 0x2027], // General punctuation: dashes, quotes, ellipsis
    [0x2030, 0x205e],
    [0x2190, 0x21ff], // Arrows (occasionally used in prose, e.g. "A -> B")
];

const FORBIDDEN_EMAIL_ALLOWLIST = new Set(["imcooder@gmail.com"]);

// Matches "Co-Authored-By:" (any casing/spacing) trailers outright — this project's commits
// should show a single human author, not AI co-authorship.
const CO_AUTHORED_BY_RE = /^\s*co-authored-by\s*:/im;

const EMAIL_RE = /[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}/g;

// Case-insensitive; deliberately broad so a new frontier-model name doesn't slip through
// unnoticed just because this list wasn't updated for it.
const MODEL_NAME_RE = /\b(claude|anthropic|chatgpt|openai|gpt-?\d|gemini|copilot|llama|mistral|deepseek|qwen|sonnet|opus|haiku)\b/i;

function isAllowedChar(codePoint) {
    return ALLOWED_CHAR_RANGES.some(([lo, hi]) => codePoint >= lo && codePoint <= hi);
}

export function findDisallowedCharacters(message) {
    const found = new Set();
    for (const ch of message) {
        const cp = ch.codePointAt(0);
        if (!isAllowedChar(cp)) {
            found.add(ch);
        }
    }
    return [...found];
}

export function findForbiddenEmails(message) {
    const matches = message.match(EMAIL_RE) || [];
    return matches.filter((email) => !FORBIDDEN_EMAIL_ALLOWLIST.has(email.toLowerCase()));
}

export function findModelNameMentions(message) {
    const matches = message.match(new RegExp(MODEL_NAME_RE, "gi"));
    return matches ? [...new Set(matches.map((m) => m.toLowerCase()))] : [];
}

export function hasCoAuthoredByTrailer(message) {
    return CO_AUTHORED_BY_RE.test(message);
}

// Returns an array of human-readable problem strings; empty array means the message passes.
export function checkMessage(message) {
    const problems = [];

    if (hasCoAuthoredByTrailer(message)) {
        problems.push('contains a "Co-Authored-By:" trailer — this repo\'s commits must show a single human author');
    }

    const models = findModelNameMentions(message);
    if (models.length > 0) {
        problems.push(`mentions an AI model/vendor name: ${models.join(", ")}`);
    }

    const emails = findForbiddenEmails(message);
    if (emails.length > 0) {
        problems.push(`contains an email address not in the allowlist: ${emails.join(", ")}`);
    }

    const badChars = findDisallowedCharacters(message);
    if (badChars.length > 0) {
        problems.push(`contains non-English characters: ${badChars.slice(0, 20).join(" ")}${badChars.length > 20 ? " ..." : ""}`);
    }

    return problems;
}
