# CeeBase Platform & BaaS Developer Guide

CeeBase is an open-source database and Backend as a Service (BaaS) platform designed to package the CeeBase C++20 database engine into an intuitive, scalable, developer-friendly backend ecosystem.

---

## 1. Overview & Core Interfaces

CeeBase provides three unified developer interfaces backed by a shared platform API:

1. **CeeBase CLI**: Cross-platform command-line utility written in Rust (`ceebase`) providing project management, migrations, DB shell, secrets, and deployments.
2. **CeeBase Studio**: Full web management dashboard running locally or in the cloud, featuring:
   - Visual Table Browser (schema inspection, inline row editor)
   - Interactive SQL Editor with query metrics
   - Authentication & User Management with Session tracking
   - Row-Level Security (RLS) Policy Editor
   - S3-compatible Object Storage Bucket & Asset Browser
   - Realtime Changefeed Event Inspector (WebSocket / SSE)
   - Serverless Functions & Background Jobs Runtime
   - REST API Explorer with auto-generated documentation
   - Logs & System Resource Monitoring
   - 4 KB Slotted Page Diagnostic & Inspect Tool
   - Project Settings and API Key Management
3. **CeeBase Desktop**: Native desktop GUI application embedding CeeBase Studio with local process lifecycle management.

---

## 2. API Gateway & Endpoints

All project traffic passes through the CeeBase API Gateway routing under standard prefix paths:

| Endpoint | Protocol | Description |
| :--- | :--- | :--- |
| `/auth/v1/signup` | POST | Register new project user with password hashing |
| `/auth/v1/token?grant_type=password` | POST | Authenticate user and receive JWT session & refresh token |
| `/auth/v1/token?grant_type=refresh_token` | POST | Rotate refresh token and issue new JWT |
| `/auth/v1/otp` | POST | Request passwordless login token |
| `/auth/v1/user` | GET / PUT | Inspect or update current authenticated user profile |
| `/auth/v1/logout` | POST | Revoke user session and invalidate refresh token |
| `/rest/v1/:table` | GET | Query table rows with filters (`?col=eq.val`, `?limit=N`) |
| `/rest/v1/:table` | POST | Insert one or more records into the relation |
| `/rest/v1/:table` | PATCH | Update records matching filter criteria |
| `/rest/v1/:table` | DELETE | Delete records matching filter criteria |
| `/storage/v1/bucket` | GET / POST | List or create object storage buckets |
| `/storage/v1/object/:bucket/:file` | GET / POST / DELETE | Upload, download, or delete storage objects |
| `/storage/v1/object/sign/:bucket/:file` | POST | Generate time-limited signed download URLs |
| `/functions/v1/:function_name` | POST | Execute serverless function with environment secrets |
| `/realtime/v1` | WebSocket / GET | Subscribe to real-time database changefeeds |
| `/api/v1/projects` | GET / POST / DELETE | Control plane project provisioning and lifecycle |

---

## 3. Row-Level Security (RLS)

CeeBase includes an in-engine Row-Level Security engine. When RLS is enabled on a table, all incoming queries executed with the `anon` or `authenticated` role are evaluated against active policies.

### Helper Contexts
- `auth.uid()`: Current user ID from the verified JWT `sub` claim.
- `auth.role()`: Current user role (`anon`, `authenticated`, or `service_role`).
- `auth.email()`: Current user email address.

### Example Policies
```sql
-- Allow users to view their own profile records
CREATE POLICY "user_view_own" ON profiles FOR SELECT USING (user_id = auth.uid());

-- Allow team members to update their assigned issues
CREATE POLICY "team_update_issues" ON issues FOR UPDATE USING (assignee_id = auth.uid());
```

Requests authenticated with the `service_role` key bypass RLS policies for administrative operations.

---

## 4. Client SDKs

### 4.1 TypeScript / JavaScript (`@ceebase/sdk`)

Install via npm or use directly from source:
```bash
npm install @ceebase/sdk
```

```typescript
import { createClient } from '@ceebase/sdk';

const ceebase = createClient({
  url: 'http://127.0.0.1:8080',
  key: 'eyJhbGciOiJIUzI1Ni...' // anon_key
});

// 1. Authentication
const { session, user } = await ceebase.auth.signUp('developer@ceebase.io', 'SecurePass123!');

// 2. Database Queries
const { data, error } = await ceebase
  .from('missions')
  .insert({ id: 1, title: 'Explore Mars', status: 'active' });

const { data: rows } = await ceebase
  .from('missions')
  .select('*')
  .eq('status', 'active');

// 3. Storage
await ceebase.storage.from('media').upload('mission.jpg', fileBytes, 'image/jpeg');
const { signedUrl } = await ceebase.storage.from('media').createSignedUrl('mission.jpg', 3600);

// 4. Serverless Functions
const { result } = await ceebase.functions.invoke('calculate-telemetry', { distance: 54.6 });

// 5. Realtime Subscriptions
const channel = ceebase.channel('missions')
  .on('INSERT', (payload) => console.log('New mission:', payload))
  .subscribe();
```

### 4.2 Python SDK (`ceebase`)

```python
from ceebase import create_client

client = create_client(
    url="http://127.0.0.1:8080",
    key="eyJhbGciOiJIUzI1Ni..."
)

# Authentication
auth_res = client.auth.sign_up("pilot@ceebase.io", "Passwd987#")

# Database Queries
res = client.from_table("missions").insert({"id": 2, "title": "Lunar Base", "status": "planning"})
rows = client.from_table("missions").select().eq("status", "planning").execute()

# Storage
client.storage.from_bucket("documents").upload("manifest.pdf", b"%PDF-1.4...", "application/pdf")
```

---

## 5. CLI Usage Guide

The `ceebase` CLI provides complete developer ergonomics:

```bash
# Authenticate with CeeBase Cloud or local cluster
ceebase login --token <personal-access-token>

# Project Management
ceebase projects list
ceebase projects create --name "Production Backend" --org "org-alpha"
ceebase projects link --project-id <proj-id>

# Database Operations & Migrations
ceebase db connect
ceebase db shell
ceebase migration create --name add_users_table
ceebase migration up
ceebase migration status
ceebase db dump --output backup.sql

# Storage & Functions
ceebase storage buckets
ceebase storage upload --bucket assets --file logo.png
ceebase functions deploy --name process-order --file ./functions/order.py
ceebase functions invoke --name process-order --data '{"order_id": 101}'

# Secrets & System Diagnostics
ceebase secrets set STRIPE_KEY=sk_test_12345
ceebase doctor
```

---

## 6. Self-Hosted Deployment

CeeBase can be deployed anywhere with Docker Compose or Kubernetes:

```bash
docker compose up -d
```

The stack exposes:
- **API Gateway & Studio**: Port `8080`
- **Native TCP Database Protocol**: Port `5433`
- **Prometheus Metrics**: Port `9090`
- **Storage Volumes**: `/var/lib/ceebase/data`
