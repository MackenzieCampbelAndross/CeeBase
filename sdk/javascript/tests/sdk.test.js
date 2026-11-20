const test = require("node:test");
const assert = require("node:assert/strict");
const { createClient } = require("../index.js");

const SERVER_URL = "http://127.0.0.1:8080";
const ANON_KEY = "test_anon_key";

test("CeeBase JavaScript SDK: client initialization", () => {
  const ceebase = createClient({
    url: SERVER_URL,
    key: ANON_KEY
  });

  assert.equal(ceebase.url, SERVER_URL);
  assert.equal(ceebase.key, ANON_KEY);
  assert.ok(ceebase.auth);
  assert.ok(ceebase.storage);
  assert.ok(ceebase.functions);
});

test("CeeBase JavaScript SDK: query builder construction", () => {
  const ceebase = createClient(SERVER_URL, ANON_KEY);
  const query = ceebase.from("users")
    .select("id, name, balance")
    .eq("id", 1)
    .order("id", { ascending: false })
    .limit(10);

  assert.equal(query.table, "users");
  assert.equal(query.queryParams["select"], "id, name, balance");
  assert.equal(query.queryParams["id"], "eq.1");
  assert.equal(query.queryParams["order"], "id.desc");
  assert.equal(query.queryParams["limit"], "10");
});

test("CeeBase JavaScript SDK: live database query execution", async () => {
  const ceebase = createClient(SERVER_URL, ANON_KEY);
  const { data, error } = await ceebase.from("users").select("*");

  assert.equal(error, null);
  assert.ok(Array.isArray(data));
  assert.ok(data.length >= 1);
  assert.ok(data[0].id !== undefined);
});

test("CeeBase JavaScript SDK: storage signed URL creation", async () => {
  const ceebase = createClient(SERVER_URL, ANON_KEY);
  const { data, error } = await ceebase.storage.from("media").createSignedUrl("banner.png", 120);

  assert.equal(error, null);
  assert.ok(data.signedUrl);
  assert.ok(data.signedUrl.includes("/storage/v1/object/"));
  assert.ok(data.signedUrl.includes("token="));
});

test("CeeBase JavaScript SDK: realtime channel subscription", (t, done) => {
  const ceebase = createClient(SERVER_URL, ANON_KEY);
  const channel = ceebase.channel("realtime:public:users")
    .on("postgres_changes", { event: "INSERT", schema: "public", table: "users" }, (payload) => {
      // callback
    })
    .subscribe((status) => {
      assert.equal(status, "SUBSCRIBED");
      done();
    });
});
