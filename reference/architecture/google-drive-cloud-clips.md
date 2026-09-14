# Google Drive Cloud Clips

`CloudUploadManager` is owned by the dock and receives only verified, immutable trimmed paths from `ReplayBufferManager`. Cloud upload is disabled by default.

## Storage and ownership

- Non-sensitive settings: `%LOCALAPPDATA%/OBSCloudClips/settings.json`
- Durable jobs: `%LOCALAPPDATA%/OBSCloudClips/queue.json`
- Staged clips: `%LOCALAPPDATA%/OBSCloudClips/temp/<job-id>/`
- OAuth credentials/tokens: `credentials.dat`, encrypted for the current Windows user with DPAPI

When delete-after-upload is enabled, the finished clip is moved into staging. Otherwise it is copied, preserving the recording-directory copy. The staged file remains until Drive metadata confirms the same byte size.

## Threads and shutdown

The OBS saved callback only feeds the existing trim worker. After trim verification, staging and atomic queue persistence occur there; Google API calls run on a separate single-upload worker. Requests use an inactivity timeout. Destruction first stops the trim callback, then stops accepting cloud jobs, persists the queue, signals the worker, and joins it. An interrupted `Uploading` job loads as `Pending` next time.

## Google API flow

OAuth uses Authorization Code + PKCE, the system browser, a loopback listener on a dynamic port, refresh tokens, and the `drive.file` scope. Upload creates/locates each folder component, creates a Drive API v3 resumable session, streams the file, fetches `id,size,trashed`, and accepts completion only when the remote size equals the local size.

Retries use 5, 15, 30, 60, then 300 seconds. A failed job and its file remain durable.
