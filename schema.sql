
CREATE TABLE IF NOT EXISTS users (
  id       SERIAL PRIMARY KEY,
  username TEXT   UNIQUE NOT NULL
);


CREATE TABLE IF NOT EXISTS uploads (
  user_id    INT NOT NULL REFERENCES users(id),
  path       TEXT    NOT NULL,
  chunk      INT     NOT NULL,
  message_id BIGINT NOT NULL, 
  PRIMARY KEY(user_id, path, chunk)
);