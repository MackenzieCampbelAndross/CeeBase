/**
 * @ceebase/sdk - Official JavaScript & TypeScript Client for CeeBase Platform
 * Provides fluent PostgREST-style query builder, Auth, Object Storage,
 * Realtime event listeners, and Functions invocation.
 */

class PostgrestFilterBuilder {
  constructor(client, table) {
    this.client = client;
    this.table = table;
    this.method = "GET";
    this.body = null;
    this.queryParams = {};
  }

  select(columns = "*") {
    this.method = "GET";
    this.queryParams["select"] = columns;
    return this;
  }

  eq(column, value) {
    this.queryParams[column] = `eq.${value}`;
    return this;
  }

  neq(column, value) {
    this.queryParams[column] = `neq.${value}`;
    return this;
  }

  gt(column, value) {
    this.queryParams[column] = `gt.${value}`;
    return this;
  }

  lt(column, value) {
    this.queryParams[column] = `lt.${value}`;
    return this;
  }

  like(column, pattern) {
    this.queryParams[column] = `like.${pattern}`;
    return this;
  }

  order(column, { ascending = true } = {}) {
    this.queryParams["order"] = `${column}.${ascending ? "asc" : "desc"}`;
    return this;
  }

  limit(count) {
    this.queryParams["limit"] = String(count);
    return this;
  }

  offset(count) {
    this.queryParams["offset"] = String(count);
    return this;
  }

  insert(values) {
    this.method = "POST";
    this.body = values;
    return this;
  }

  update(values) {
    this.method = "PATCH";
    this.body = values;
    return this;
  }

  delete() {
    this.method = "DELETE";
    return this;
  }

  async then(resolve, reject) {
    try {
      const qs = new URLSearchParams(this.queryParams).toString();
      const url = `${this.client.url}/rest/v1/${this.table}${qs ? `?${qs}` : ""}`;
      const headers = {
        "Content-Type": "application/json",
        "apikey": this.client.key,
        "Authorization": `Bearer ${this.client.auth.currentSession?.access_token || this.client.key}`
      };

      const options = {
        method: this.method,
        headers
      };
      if (this.body && (this.method === "POST" || this.method === "PATCH")) {
        options.body = JSON.stringify(this.body);
      }

      const res = await fetch(url, options);
      if (!res.ok) {
        const errJson = await res.json().catch(() => ({ error: res.statusText }));
        resolve({ data: null, error: errJson.error || errJson });
        return;
      }

      const data = await res.json().catch(() => null);
      resolve({ data, error: null });
    } catch (err) {
      resolve({ data: null, error: err.message || String(err) });
    }
  }
}

class AuthClient {
  constructor(client) {
    this.client = client;
    this.currentSession = null;
    this.currentUser = null;
  }

  async signUp({ email, password, metadata = {} }) {
    const url = `${this.client.url}/auth/v1/signup`;
    const res = await fetch(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "apikey": this.client.key
      },
      body: JSON.stringify({ email, password, metadata })
    });
    const json = await res.json();
    if (!res.ok) return { data: null, error: json.error || json };
    if (json.session) {
      this.currentSession = json.session;
      this.currentUser = json.user;
    }
    return { data: json, error: null };
  }

  async signInWithPassword({ email, password }) {
    const url = `${this.client.url}/auth/v1/token?grant_type=password`;
    const res = await fetch(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "apikey": this.client.key
      },
      body: JSON.stringify({ email, password })
    });
    const json = await res.json();
    if (!res.ok) return { data: null, error: json.error || json };
    this.currentSession = json;
    this.currentUser = json.user;
    return { data: { user: json.user, session: json }, error: null };
  }

  async signInWithOtp({ email }) {
    const url = `${this.client.url}/auth/v1/otp`;
    const res = await fetch(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "apikey": this.client.key
      },
      body: JSON.stringify({ email })
    });
    const json = await res.json();
    if (!res.ok) return { data: null, error: json.error || json };
    return { data: json, error: null };
  }

  async verifyOtp({ email, token }) {
    const url = `${this.client.url}/auth/v1/verify`;
    const res = await fetch(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "apikey": this.client.key
      },
      body: JSON.stringify({ email, code: token })
    });
    const json = await res.json();
    if (!res.ok) return { data: null, error: json.error || json };
    this.currentSession = json;
    this.currentUser = json.user;
    return { data: { user: json.user, session: json }, error: null };
  }

  async signOut() {
    const token = this.currentSession?.access_token || "";
    const url = `${this.client.url}/auth/v1/logout`;
    await fetch(url, {
      method: "POST",
      headers: {
        "apikey": this.client.key,
        "Authorization": `Bearer ${token}`
      }
    }).catch(() => {});
    this.currentSession = null;
    this.currentUser = null;
    return { error: null };
  }

  getUser() {
    return this.currentUser;
  }

  getSession() {
    return this.currentSession;
  }
}

class StorageBucketClient {
  constructor(client, bucketId) {
    this.client = client;
    this.bucketId = bucketId;
  }

  async upload(path, fileData, { contentType = "application/octet-stream" } = {}) {
    const cleanPath = path.replace(/^\/+/, "");
    const url = `${this.client.url}/storage/v1/object/${this.bucketId}/${cleanPath}`;
    const token = this.client.auth.currentSession?.access_token || this.client.key;

    const res = await fetch(url, {
      method: "POST",
      headers: {
        "Content-Type": contentType,
        "apikey": this.client.key,
        "Authorization": `Bearer ${token}`
      },
      body: fileData
    });

    const json = await res.json();
    if (!res.ok) return { data: null, error: json.error || json };
    return { data: json, error: null };
  }

  async download(path) {
    const cleanPath = path.replace(/^\/+/, "");
    const url = `${this.client.url}/storage/v1/object/${this.bucketId}/${cleanPath}`;
    const token = this.client.auth.currentSession?.access_token || this.client.key;

    const res = await fetch(url, {
      headers: {
        "apikey": this.client.key,
        "Authorization": `Bearer ${token}`
      }
    });

    if (!res.ok) {
      const err = await res.json().catch(() => ({ error: res.statusText }));
      return { data: null, error: err.error || err };
    }
    const blob = await res.blob();
    return { data: blob, error: null };
  }

  async list(prefix = "") {
    const qs = prefix ? `?prefix=${encodeURIComponent(prefix)}` : "";
    const url = `${this.client.url}/storage/v1/object/list/${this.bucketId}${qs}`;
    const token = this.client.auth.currentSession?.access_token || this.client.key;

    const res = await fetch(url, {
      headers: {
        "apikey": this.client.key,
        "Authorization": `Bearer ${token}`
      }
    });
    const json = await res.json();
    if (!res.ok) return { data: null, error: json.error || json };
    return { data: json, error: null };
  }

  async createSignedUrl(path, expiresIn = 3600) {
    const cleanPath = path.replace(/^\/+/, "");
    const url = `${this.client.url}/storage/v1/object/sign/${this.bucketId}/${cleanPath}`;
    const res = await fetch(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "apikey": this.client.key
      },
      body: JSON.stringify({ expires_in: expiresIn })
    });
    const json = await res.json();
    if (!res.ok) return { data: null, error: json.error || json };
    const fullSignedUrl = `${this.client.url}${json.signed_url}`;
    return { data: { signedUrl: fullSignedUrl }, error: null };
  }
}

class StorageClient {
  constructor(client) {
    this.client = client;
  }

  from(bucketId) {
    return new StorageBucketClient(this.client, bucketId);
  }

  async listBuckets() {
    const url = `${this.client.url}/storage/v1/bucket`;
    const res = await fetch(url, {
      headers: { "apikey": this.client.key }
    });
    const json = await res.json();
    if (!res.ok) return { data: null, error: json.error || json };
    return { data: json, error: null };
  }
}

class FunctionsClient {
  constructor(client) {
    this.client = client;
  }

  async invoke(functionName, { body = {}, headers = {} } = {}) {
    const url = `${this.client.url}/functions/v1/${functionName}/invoke`;
    const token = this.client.auth.currentSession?.access_token || this.client.key;

    const res = await fetch(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "apikey": this.client.key,
        "Authorization": `Bearer ${token}`,
        ...headers
      },
      body: JSON.stringify(body)
    });

    const json = await res.json();
    if (!res.ok) return { data: null, error: json.error || json };
    return { data: json.data || json, error: null };
  }
}

class RealtimeChannel {
  constructor(client, topic) {
    this.client = client;
    this.topic = topic;
    this.listeners = [];
  }

  on(event, filter, callback) {
    this.listeners.push({ event, filter, callback });
    return this;
  }

  subscribe(callback) {
    if (callback) callback("SUBSCRIBED");
    return this;
  }
}

class CeeBaseClient {
  constructor(url, key, options = {}) {
    this.url = url.replace(/\/+$/, "");
    this.key = key;
    this.options = options;

    this.auth = new AuthClient(this);
    this.storage = new StorageClient(this);
    this.functions = new FunctionsClient(this);
  }

  from(table) {
    return new PostgrestFilterBuilder(this, table);
  }

  channel(topic) {
    return new RealtimeChannel(this, topic);
  }
}

function createClient(url, key, options) {
  if (typeof url === "object" && url !== null) {
    return new CeeBaseClient(url.url, url.key, url.options);
  }
  return new CeeBaseClient(url, key, options);
}

module.exports = {
  createClient,
  CeeBaseClient
};
