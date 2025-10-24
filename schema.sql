-- Users
CREATE TABLE IF NOT EXISTS users (
  id        SERIAL PRIMARY KEY,
  username  TEXT UNIQUE NOT NULL
);



-- Nodes for files & folders
CREATE TABLE IF NOT EXISTS nodes (
  id         SERIAL PRIMARY KEY,
  user_id    INT    NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  name       TEXT   NOT NULL CHECK (position('/' IN name) = 0),  -- make sure name's valid
  parent_id  INT    REFERENCES nodes(id) ON DELETE CASCADE,  
  type       SMALLINT NOT NULL,  -- 1=file, 2=folder
  size       BIGINT NOT NULL DEFAULT 0,
  ready      BOOLEAN NOT NULL DEFAULT FALSE,
  i_atime    BIGINT,
  i_mtime    BIGINT NOT NULL DEFAULT 0,
  i_ctime    BIGINT,
  i_crtime   BIGINT
);

-- Enfore unique names per directory, user
-- DEFERRABLE INITIALLY IMMEDIATE for swaps
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
     WHERE conname = 'uq_nodes_dirname'
       AND conrelid = 'nodes'::regclass
  ) THEN
    ALTER TABLE nodes
      ADD CONSTRAINT uq_nodes_dirname
      UNIQUE (user_id, parent_id, name)
      DEFERRABLE INITIALLY IMMEDIATE;
  END IF;
END$$;




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


-- Indexes for queries
CREATE INDEX IF NOT EXISTS idx_nodes_dirlist
  ON nodes (user_id, parent_id, type DESC, name)
  INCLUDE (i_mtime);

CREATE INDEX IF NOT EXISTS idx_nodes_parent
  ON nodes (parent_id);

CREATE INDEX IF NOT EXISTS idx_cl_desc
  ON node_closure (descendant);

CREATE INDEX IF NOT EXISTS idx_cl_anc
  ON node_closure (ancestor);

CREATE INDEX IF NOT EXISTS idx_chunks_node
  ON file_chunks (node_id);
