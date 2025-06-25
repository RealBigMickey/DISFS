-- Users
CREATE TABLE IF NOT EXISTS users (
  id        SERIAL PRIMARY KEY,
  username  TEXT UNIQUE NOT NULL
);

-- Nodes for files & folders
CREATE TABLE IF NOT EXISTS nodes (
  id         SERIAL PRIMARY KEY,
  user_id    INT    NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  name       TEXT   NOT NULL,
  parent_id  INT    REFERENCES nodes(id) ON DELETE CASCADE,  
  type       SMALLINT NOT NULL,  -- 1=file, 2=folder
  message_id BIGINT,
  i_atime    BIGINT,
  i_mtime    BIGINT,
  i_ctime    BIGINT,
  i_crtime   BIGINT
);

-- Closure table
CREATE TABLE IF NOT EXISTS node_closure (
  ancestor    INT NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
  descendant  INT NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
  depth       INT NOT NULL,
  PRIMARY KEY (ancestor, descendant)
);

-- File chunks
CREATE TABLE IF NOT EXISTS file_chunks (
  node_id     INT    NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,  -- must be type=1
  chunk_index INT    NOT NULL,
  chunk_size  INT,
  message_id  BIGINT, 
  PRIMARY KEY (node_id, chunk_index)
);
