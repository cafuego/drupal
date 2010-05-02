<?php
// $Id: query.inc,v 1.15 2010/04/19 04:43:05 webchick Exp $

/**
 * @ingroup database
 * @{
 */

/**
 * @file
 * Query code for MySQL embedded database engine.
 */


class InsertQuery_mysql extends InsertQuery {

  public function execute() {
    if (!$this->preExecute()) {
      return NULL;
    }

    // If we're selecting from a SelectQuery, finish building the query and
    // pass it back, as any remaining options are irrelevant.
    if (empty($this->fromQuery)) {
      $max_placeholder = 0;
      $values = array();
      foreach ($this->insertValues as $insert_values) {
        foreach ($insert_values as $value) {
          $values[':db_insert_placeholder_' . $max_placeholder++] = $value;
        }
      }
    }
    else {
      $values = $this->fromQuery->getArguments();
    }

    $last_insert_id = $this->connection->query((string)$this, $values, $this->queryOptions);

    // Re-initialize the values array so that we can re-use this query.
    $this->insertValues = array();

    return $last_insert_id;
  }

  public function __toString() {

    // Default fields are always placed first for consistency.
    $insert_fields = array_merge($this->defaultFields, $this->insertFields);

    // If we're selecting from a SelectQuery, finish building the query and
    // pass it back, as any remaining options are irrelevant.
    if (!empty($this->fromQuery)) {
      return "INSERT INTO {" . $this->table . '} (' . implode(', ', $insert_fields) . ') ' . $this->fromQuery;
    }

    $query = "INSERT INTO {" . $this->table . '} (' . implode(', ', $insert_fields) . ') VALUES ';

    $max_placeholder = 0;
    $values = array();
    if (count($this->insertValues)) {
      foreach ($this->insertValues as $insert_values) {
        $placeholders = array();

        // Default fields aren't really placeholders, but this is the most convenient
        // way to handle them.
        $placeholders = array_pad($placeholders, count($this->defaultFields), 'default');

        $new_placeholder = $max_placeholder + count($insert_values);
        for ($i = $max_placeholder; $i < $new_placeholder; ++$i) {
          $placeholders[] = ':db_insert_placeholder_' . $i;
        }
        $max_placeholder = $new_placeholder;
        $values[] = '(' . implode(', ', $placeholders) . ')';
      }
    }
    else {
      // If there are no values, then this is a default-only query. We still need to handle that.
      $placeholders = array_fill(0, count($this->defaultFields), 'default');
      $values[] = '(' . implode(', ', $placeholders) . ')';
    }

    $query .= implode(', ', $values);

    return $query;
  }
}

class MergeQuery_mysql extends MergeQuery {

  public function execute() {

    // A merge query without any key field is invalid.
    if (count($this->keyFields) == 0) {
      throw new InvalidMergeQueryException("You need to specify key fields before executing a merge query");
    }

    // Set defaults.
    if ($this->updateFields) {
      $update_fields = $this->updateFields;
    }
    else {
      // When update fields are derived from insert fields, we don't need
      // placeholders since we can tell MySQL to reuse insert supplied
      // values using the VALUES(col_name) function.
      $update_fields = array();
    }

    $insert_fields = $this->insertFields + $this->keyFields;

    $max_placeholder = 0;
    $values = array();
    // We assume that the order here is the same as in __toString(). If that's
    // not the case, then we have serious problems.
    foreach ($insert_fields as $value) {
      $values[':db_insert_placeholder_' . $max_placeholder++] = $value;
    }

    // Expressions take priority over literal fields, so we process those first
    // and remove any literal fields that conflict.
    foreach ($this->expressionFields as $field => $data) {
      if (!empty($data['arguments'])) {
        $values += $data['arguments'];
      }
      unset($update_fields[$field]);
    }

    // Because we filter $fields the same way here and in __toString(), the
    // placeholders will all match up properly.
    $max_placeholder = 0;
    foreach ($update_fields as $field => $value) {
      $values[':db_update_placeholder_' . ($max_placeholder++)] = $value;
    }


    // MySQL's INSERT ... ON DUPLICATE KEY UPDATE queries return 1
    // (MergeQuery::STATUS_INSERT) for an INSERT operation or 2
    // (MergeQuery::STATUS_UPDATE) for an UPDATE operation.
    //
    // @link http ://dev.mysql.com/doc/refman/5.0/en/mysql-affected-rows.html
    $this->queryOptions['return'] = Database::RETURN_AFFECTED;
    return $this->connection->query((string)$this, $values, $this->queryOptions);
  }


  public function __toString() {

    // Set defaults.
    if ($this->updateFields) {
      $update_fields = $this->updateFields;
    }
    else {
      $update_fields = $this->insertFields;
      // If there are no exclude fields, this is a no-op.
      foreach ($this->excludeFields as $exclude_field) {
        unset($update_fields[$exclude_field]);
      }
    }

    // If the merge query has no fields to update, add the first key as an
    // update field so the query will not fail if a duplicate key is found.
    if (!$update_fields && !$this->expressionFields) {
      $update_fields = array_slice($this->keyFields, 0, 1, TRUE);
    }

    $insert_fields = $this->insertFields + $this->keyFields;

    $query = "INSERT INTO {" . $this->table . '} (' . implode(', ', array_keys($insert_fields)) . ') VALUES ';

    $max_placeholder = 0;
    $values = array();
    // We don't need the $field, but this is a convenient way to count.
    foreach ($insert_fields as $field) {
      $values[] = ':db_insert_placeholder_' . $max_placeholder++;
    }

    $query .= '(' . implode(', ', $values) . ') ON DUPLICATE KEY UPDATE ';

    // Expressions take priority over literal fields, so we process those first
    // and remove any literal fields that conflict.
    $max_placeholder = 0;
    $update = array();
    foreach ($this->expressionFields as $field => $data) {
      $update[] = $field . '=' . $data['expression'];
      unset($update_fields[$field]);
    }

    // Build update fields clauses based on caller supplied list, or derived
    // from insert supplied values using the VALUES(col_name) function.
    foreach ($update_fields as $field => $value) {
      if ($this->updateFields) {
        $update[] = ($field . '=:db_update_placeholder_' . $max_placeholder++);
      }
      else {
        $update[] = ($field . '=VALUES(' . $field . ')');
      }
    }

    $query .= implode(', ', $update);

    return $query;
  }
}

class SelectQuery_mysql extends SelectQuery {

  // MySQL allows for query options. These are string literals that ask the server
  // for specific goo.
  private $flags = array();

  // Set flag. Do this by key, so the sme flag can't be set twice.
  public function addFlag($flag) {
    $this->flags[$flag] = $flag;
  }

  // Return the flags array.
  public function &flags() {
    return $this->flags;
  }

  /**
   * Override this for SelectQuery_mysql as we can have flags!
   */
  public function __toString() {

    // SELECT
    $query .= 'SELECT ';

    if (!empty($this->flags)) {
      $query .= implode(' ', $this->flags) . ' ';
    }

    if ($this->distinct) {
      $query .= 'DISTINCT ';
    }

    // FIELDS and EXPRESSIONS
    $fields = array();
    foreach ($this->tables as $alias => $table) {
      if (!empty($table['all_fields'])) {
        $fields[] = $alias . '.*';
      }
    }
    foreach ($this->fields as $alias => $field) {
      // Always use the AS keyword for field aliases, as some
      // databases require it (e.g., PostgreSQL).
      $fields[] = (isset($field['table']) ? $field['table'] . '.' : '') . $field['field'] . ' AS ' . $field['alias'];
    }
    foreach ($this->expressions as $alias => $expression) {
      $fields[] = $expression['expression'] . ' AS ' . $expression['alias'];
    }
    $query .= implode(', ', $fields);


    // FROM - We presume all queries have a FROM, as any query that doesn't won't need the query builder anyway.
    $query .= "\nFROM ";
    foreach ($this->tables as $alias => $table) {
      $query .= "\n";
      if (isset($table['join type'])) {
        $query .= $table['join type'] . ' JOIN ';
      }

      // If the table is a subquery, compile it and integrate it into this query.
      if ($table['table'] instanceof SelectQueryInterface) {
        // Run preparation steps on this sub-query before converting to string.
        $subquery = $table['table'];
        $subquery->preExecute();
        $table_string = '(' . (string) $subquery . ')';
      }
      else {
        $table_string = '{' . $this->connection->escapeTable($table['table']) . '}';
      }

      // Don't use the AS keyword for table aliases, as some
      // databases don't support it (e.g., Oracle).
      $query .=  $table_string . ' ' . $table['alias'];

      if (!empty($table['condition'])) {
        $query .= ' ON ' . $table['condition'];
      }
    }

    // WHERE
    if (count($this->where)) {
      $this->where->compile($this->connection, $this);
      // There is an implicit string cast on $this->condition.
      $query .= "\nWHERE " . $this->where;
    }

    // GROUP BY
    if ($this->group) {
      $query .= "\nGROUP BY " . implode(', ', $this->group);
    }

    // HAVING
    if (count($this->having)) {
      $this->having->compile($this->connection, $this);
      // There is an implicit string cast on $this->having.
      $query .= "\nHAVING " . $this->having;
    }

    // ORDER BY
    if ($this->order) {
      $query .= "\nORDER BY ";
      $fields = array();
      foreach ($this->order as $field => $direction) {
        $fields[] = $field . ' ' . $direction;
      }
      $query .= implode(', ', $fields);
    }

    // RANGE
    // There is no universal SQL standard for handling range or limit clauses.
    // Fortunately, all core-supported databases use the same range syntax.
    // Databases that need a different syntax can override this method and
    // do whatever alternate logic they need to.
    if (!empty($this->range)) {
      $query .= "\nLIMIT " . $this->range['length'] . " OFFSET " . $this->range['start'];
    }

    // UNION is a little odd, as the select queries to combine are passed into
    // this query, but syntactically they all end up on the same level.
    if ($this->union) {
      foreach ($this->union as $union) {
        $query .= ' ' . $union['type'] . ' ' . (string) $union['query'];
      }
    }

    return $query;
  }
}


/**
 * @} End of "ingroup database".
 */
