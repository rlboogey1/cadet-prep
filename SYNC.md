# Online sync

Sign in to the same account on the Mac, iPhone and iPad, and practice history, missed
questions, AFOQT flashcard boxes, the spaced repetition deck and its streak stay the same
on all of them. Settings like dark mode and sound stay separate on each device.

The account and progress live in a free [Supabase](https://supabase.com) project that
only you can sign in to. You set it up once (about 10 minutes).

## 1. Make the Supabase project

1. Go to supabase.com, sign up, and click **New project**. Any name and region work, and
   the free plan is enough. Write down the database password it asks for, though the app
   never needs it.
2. When the project is ready, open **SQL Editor**, paste this in, and click **Run**:

   ```sql
   create table public.progress (
     user_id uuid primary key references auth.users on delete cascade default auth.uid(),
     data jsonb not null,
     updated_at timestamptz not null default now()
   );
   alter table public.progress enable row level security;
   create policy "Each account sees only its own progress" on public.progress
     for all using (auth.uid() = user_id) with check (auth.uid() = user_id);
   ```

3. Make your account: **Authentication > Users > Add user > Create new user**. Enter
   your email and a password, and check **Auto Confirm User**.
4. Turn off sign-ups so nobody else can make an account: **Authentication > Sign In /
   Providers**, then switch off **Allow new users to sign up** and save.
5. Get the address and key: **Project Settings > API Keys** (or the **Connect**
   button). You need the **Project URL** (`https://xxxx.supabase.co`) and the **anon
   public** key (or the **publishable** key on newer projects). Don't use the
   `service_role` or secret key.

## 2. Give them to the app

Make a file called `supabase.json` in the project folder (next to `CMakeLists.txt`):

```json
{
  "url": "https://xxxx.supabase.co",
  "key": "your anon public key"
}
```

It's in `.gitignore`, so it won't be committed. The public key is safe to put in the app.
The table rules above only let a signed-in account see its own row, and sign-ups are off.

## 3. Rebuild both apps

Mac:

```bash
cmake -S . -B build && cmake --build build
```

iPhone/iPad: configure again so it picks up `supabase.json`, then install it as in
`IOS.md`:

```bash
cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS
```

## 4. Sign in

On each device, open **Settings > Account** and sign in. The first sign-in uploads that
device's progress. Each later sign-in merges what's online with what's on that device.
Nothing is thrown away. Sign in on the Mac first, then on the iPhone.

## How merging works

- **Practice sessions:** every session from every device, each counted once.
- **Missed questions:** for each question, the latest result wins. Missing a question on
  the Mac and later getting it right on the iPhone takes it off the review list everywhere.
- **Flashcards:** for each word, the latest change wins. A word you miss on the phone
  drops back to box 0 on the Mac too.
- **Spaced repetition deck:** the word list comes from whichever device added or removed
  words last. The streak comes from whichever device finished the deck on the later day,
  and the best streak is the highest from any device.

Sync runs on its own: when the app opens or comes back to the front, a few seconds after
you finish practicing or flipping cards, and every 10 minutes while it's open. Without a
connection, progress keeps saving on the device and goes up at the next sync.

Signing out keeps this device's progress; it just stops syncing. The sign-in is saved in
`afoqt_account.json` in the progress folder. Your password is never saved.
