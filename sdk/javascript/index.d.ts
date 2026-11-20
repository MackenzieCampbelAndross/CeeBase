export interface ClientOptions {
  auth?: {
    autoRefreshToken?: boolean;
    persistSession?: boolean;
  };
}

export interface User {
  id: string;
  email: string;
  metadata?: Record<string, any>;
  created_at?: number;
}

export interface Session {
  access_token: string;
  token_type: string;
  expires_in: number;
  refresh_token: string;
  user: User;
}

export interface AuthResponse {
  data: { user: User | null; session: Session | null } | null;
  error: any | null;
}

export interface StorageResponse<T> {
  data: T | null;
  error: any | null;
}

export class PostgrestFilterBuilder<T = any> implements PromiseLike<{ data: T[] | null; error: any | null }> {
  select(columns?: string): this;
  eq(column: string, value: any): this;
  neq(column: string, value: any): this;
  gt(column: string, value: any): this;
  lt(column: string, value: any): this;
  like(column: string, pattern: string): this;
  order(column: string, options?: { ascending?: boolean }): this;
  limit(count: number): this;
  offset(count: number): this;
  insert(values: any | any[]): this;
  update(values: any): this;
  delete(): this;
  then<TResult1 = { data: T[] | null; error: any | null }, TResult2 = never>(
    onfulfilled?: ((value: { data: T[] | null; error: any | null }) => TResult1 | PromiseLike<TResult1>) | undefined | null,
    onrejected?: ((reason: any) => TResult2 | PromiseLike<TResult2>) | undefined | null
  ): PromiseLike<TResult1 | TResult2>;
}

export class StorageBucketClient {
  upload(path: string, fileData: any, options?: { contentType?: string }): Promise<StorageResponse<any>>;
  download(path: string): Promise<StorageResponse<Blob>>;
  list(prefix?: string): Promise<StorageResponse<any[]>>;
  createSignedUrl(path: string, expiresIn?: number): Promise<StorageResponse<{ signedUrl: string }>>;
}

export class StorageClient {
  from(bucketId: string): StorageBucketClient;
  listBuckets(): Promise<StorageResponse<any[]>>;
}

export class AuthClient {
  signUp(credentials: { email: string; password: string; metadata?: Record<string, any> }): Promise<AuthResponse>;
  signInWithPassword(credentials: { email: string; password: string }): Promise<AuthResponse>;
  signInWithOtp(options: { email: string }): Promise<{ data: any; error: any }>;
  verifyOtp(options: { email: string; token: string }): Promise<AuthResponse>;
  signOut(): Promise<{ error: any }>;
  getUser(): User | null;
  getSession(): Session | null;
}

export class FunctionsClient {
  invoke(functionName: string, options?: { body?: any; headers?: Record<string, string> }): Promise<{ data: any; error: any }>;
}

export class RealtimeChannel {
  on(event: string, filter: any, callback: (payload: any) => void): this;
  subscribe(callback?: (status: string) => void): this;
}

export class CeeBaseClient {
  readonly url: string;
  readonly key: string;
  readonly auth: AuthClient;
  readonly storage: StorageClient;
  readonly functions: FunctionsClient;

  from<T = any>(table: string): PostgrestFilterBuilder<T>;
  channel(topic: string): RealtimeChannel;
}

export function createClient(
  urlOrConfig: string | { url: string; key: string; options?: ClientOptions },
  key?: string,
  options?: ClientOptions
): CeeBaseClient;
