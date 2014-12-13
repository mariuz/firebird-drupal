<?php
/**
 * @file
 * Database schema code for Firebird database servers.
 */


/**
 * @ingroup schemaapi
 * @{
 */

class DatabaseSchema_firebird extends DatabaseSchema {

  public function tableExists($table) {
    return (bool) $this->connection->query('SELECT RDB$RELATION_NAME FROM RDB$RELATIONS WHERE RDB$RELATION_NAME = '. $table, array(), array())->fetchField();
  }

  public function columnExists($table, $column) {
    return (bool) $this->connection->query('SELECT RDB$FIELD_NAME FROM RDB$RELATION_FIELDS WHERE RDB$RELATION_NAME = '. $this->connection->escapeTable($table) .'\' AND RDB$FIELD_NAME = '. $this->connection->escapeTable($column), array(), array())->fetchField();
  }


  /**
   * Generate SQL to create a new table from a Drupal schema definition.
   *
   * @param $name
   *   The name of the table to create.
   * @param $table
   *   A Schema API table definition array.
   * @return
   *   An array of SQL statements to create the table.
   */
  protected function createTableSql($name, $table) {

    $sql = "CREATE TABLE ". $name ." (\n";

    // Add the SQL statement for each field.
    foreach ($table['fields'] as $field_name => $field) {
      $sql .= $this->createFieldSql($field_name, $this->processField($field)) . ", \n";
    }

    // Remove the last comma and space.
    $sql = substr($sql, 0, -3) . "\n) ";

    $statements[] = $sql;

    // Process keys, indexes & uniques.
    $temp = array();
    $temp = $this->createKeysSql($table, $name);
    foreach( $temp as $t ) {
      $statements[] = $t;
    }

    // Process auto increment triggers.
    $temp = array();
    $temp = $this->createTriggersSql($table['fields'], $name);
    foreach( $temp as $t ) {
      $statements[] = $t;
    }

    return $statements;
  }

  /**
   * Create an SQL string for a field to be used in table creation or alteration.
   *
   * Before passing a field out of a schema definition into this function it has
   * to be processed by _db_process_field().
   *
   * @param $name
   *   Name of the field.
   * @param $spec
   *   The field specification, as per the schema data structure format.
   */
  protected function createFieldSql($name, $spec) {
    $sql = $name ." ". $spec['firebird_type'];

    if (isset($spec['length'])) {
      $sql .= '(' . $spec['length'] . ')';
    }
    elseif (isset($spec['precision']) && isset($spec['scale'])) {
      $sql .= '(' . $spec['precision'] . ', ' . $spec['scale'] . ')';
    }
    elseif ($spec['firebird_type']=='VARCHAR') {
      $sql .= '(1000)';
    }

    // $spec['default'] can be NULL, so we explicitly check for the key here.
    if (array_key_exists('default', $spec)) {
      if (is_string($spec['default'])) {
        $spec['default'] = "'" . $spec['default'] . "'";
      }
      elseif (is_null($spec['default'])) {
        $spec['default'] = 'NULL';
      }
      $sql .= ' DEFAULT ' . $spec['default'];
    }

    if (empty($spec['not null']) && !isset($spec['default'])) {
      $sql .= ' DEFAULT NULL';
    }

    if (!empty($spec['not null'])) {
      $sql .= ' NOT NULL';
    }

    return $sql;
  }

  /**
   * Set database-engine specific properties for a field.
   *
   * @param $field
   *   A field description array, as specified in the schema documentation.
   */
  protected function processField($field) {

    if (!isset($field['size'])) {
      $field['size'] = 'normal';
    }

    // Set the correct database-engine specific datatype.
    if (!isset($field['firebird_type'])) {
      $map = db_type_map();
      $field['firebird_type'] = $map[$field['type'] . ':' . $field['size']];
    }

    return $field;
  }

  public function getFieldTypeMap() {
    // Put :normal last so it gets preserved by array_flip. This makes
    // it much easier for modules (such as schema.module) to map
    // database types back into schema types.
    static $map = array(
      'varchar:normal'  => 'VARCHAR',
      'char:normal'     => 'CHAR',

      'text:tiny'       => 'VARCHAR',
      'text:small'      => 'VARCHAR',
      'text:medium'     => 'VARCHAR',
      'text:big'        => 'VARCHAR',
      'text:normal'     => 'VARCHAR',

      'serial:tiny'     => 'SMALLINT',
      'serial:small'    => 'SMALLINT',
      'serial:medium'   => 'INTEGER',
      'serial:big'      => 'BIGINT',
      'serial:normal'   => 'INTEGER',

      'int:tiny'        => 'SMALLINT',
      'int:small'       => 'SMALLINT',
      'int:medium'      => 'INTEGER',
      'int:big'         => 'BIGINT',
      'int:normal'      => 'INTEGER',

      'float:tiny'      => 'FLOAT',
      'float:small'     => 'FLOAT',
      'float:medium'    => 'FLOAT',
      'float:big'       => 'DOUBLE PRECISION',
      'float:normal'    => 'FLOAT',

      'numeric:normal'  => 'NUMERIC',

      'blob:big'        => 'BLOB',
      'blob:normal'     => 'BLOB',

      'datetime:normal' => 'TIMESTAMP',
    );
    return $map;
  }

  protected function createKeysSql($spec, $name) {
    $keys = array();

    if (!empty($spec['primary key'])) {
      $keys[] = "ALTER TABLE ". $name ." ADD CONSTRAINT PK_". $name  ." PRIMARY KEY (". $this->createKeysSqlHelper($spec['primary key']) . ")";
    }
    if (!empty($spec['unique keys'])) {
      foreach ($spec['unique keys'] as $key => $fields) {
        $keys[] = "ALTER TABLE ". $name ." ADD CONSTRAINT U_". substr($name,0,10) ."_". substr($key,0,10) ." UNIQUE (". $this->createKeysSqlHelper($fields) . ")";
      }
    }
    if (!empty($spec['indexes'])) {
      foreach ($spec['indexes'] as $index => $fields) {
        $keys[] = "CREATE INDEX I_". substr($name,0,10) ."_". substr($index,0,10) ." ON ". $name ."(". $this->createKeysSqlHelper($fields) .")";
      }
    }

    return $keys;
  }

  protected function createKeysSqlHelper($fields) {
    $ret = array();
    foreach ($fields as $field) {
      if (is_array($field)) {
        $ret[] = $field[0];
      }
      else {
        $ret[] = $field;
      }
    }
    return implode(', ', $ret);
  }

  protected function createTriggersSql($fields, $name) {
    $triggers = array();

    foreach ($fields as $field => $spec) {
      if ($spec['type'] == "serial") {
         $gen = $name ."_". $field ."_GEN";
         $triggers[] = "CREATE GENERATOR ". $gen;
         $triggers[] = "CREATE TRIGGER AI_". $name ."_". $field ." FOR ". $name ." ACTIVE BEFORE INSERT POSITION 0 AS BEGIN IF (NEW.". $field ." IS NULL) THEN NEW.". $field ." = GEN_ID(". $gen .", 1); END";
      }
    }

    return $triggers;
  }

  // TODO: Firebird doesn"t allow to rename tables.
  public function renameTable($table, $new_name) {
    update_sql("ALTER TABLE " . $table . " RENAME TO ". $new_name);
    return TRUE;
  }

  // TODO: Firebird may require to drop constraints before drop table.
  public function dropTable($table) {
    update_sql("DROP TABLE ". $table);
    return TRUE;
  }

  public function addField($table, $field, $spec, $keys_new = array()) {
    $query = "ALTER TABLE ". $table ." ADD ";
    $query .= $this->createFieldSql($field, $this->processField($spec));
    update_sql($query);
    return TRUE;
  }

  public function dropField($table, $field) {
    update_sql("ALTER TABLE ". $table ." DROP ". $field);
    return TRUE;
  }

  public function fieldSetDefault($table, $field, $default) {
    if (is_null($default)) {
      $default = "NULL";
    }
    else {
      $default = is_string($default) ? "\"$default\"" : $default;
    }

    update_sql("ALTER TABLE ". $table ." ALTER COLUMN ". $field ." SET DEFAULT ". $default);
  }

  public function fieldSetNoDefault($table, $field) {
    update_sql("ALTER TABLE ". $table ." ALTER COLUMN ". $field ." DROP DEFAULT");
  }

  public function indexExists($table, $name) {
    
    return (bool) $this->connection->query("SELECT RDB$INDEX_NAME FROM RDB$INDICES where RDB$UNIQUE_FLAG IS NULL AND RDB$FOREIGN_KEY IS NULL and RDB$INDEX_NAME=$name and RDB$RELATION_NAME=$table")->fetchField();
  }


  public function addPrimaryKey($table, $fields) {
    update_sql("ALTER TABLE ". $table ." ADD CONSTRAINT PK_". $table ." PRIMARY KEY (" . $this->createKeysSqlHelper($fields) . ")");
  }

  public function dropPrimaryKey($table) {
    update_sql("ALTER TABLE ". $table ." DROP CONSTRAINT PK_". $table);
    return TRUE;
  }

  public function addUniqueKey ($table, $name, $fields) {
    update_sql("ALTER TABLE ". $table ." ADD CONSTRAINT U_". substr($table,0,10) ."_" . substr($name,0,10) . " UNIQUE (" . $this->createKeysSqlHelper($fields) . ")");
  }

  public function dropUniqueKey($table, $name) {
    update_sql("ALTER TABLE ". $table ." DROP CONSTRAINT ". $name);
    return TRUE;
  }

  public function addIndex($table, $name, $fields) {
    $query = "CREATE INDEX I_". substr($name,0,10) ."_". substr($index,0,10) ." ON ". $table ."(" . $this->createKeysSqlHelper($fields) . ")";
    update_sql($query);
  }

  public function dropIndex($table, $name) {
    $ret[] = update_sql("DROP INDEX ". $name);
    return TRUE;
  }

  // TODO
  public function changeField($table, $field, $field_new, $spec, $keys_new = array()) {
    $sql = "ALTER TABLE ". $table ." ALTER COLUMN ". $field ." ". $this->createFieldSql($field_new, $this->processField($spec));
    if (count($keys_new)) {
      $sql .= ", ADD " . implode(", ADD ", $this->createKeysSql($keys_new));
    }
    update_sql($sql);
  }

}

/**
 * @} End of "ingroup schemaapi".
 */
