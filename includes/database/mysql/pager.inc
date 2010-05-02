<?php
// $Id$

/**
 * @file
 * Functions to aid in presenting mysql database results as a set of pages.
 */


/**
 * Query extender for mysql pager queries.
 *
 * This is the mysql specific pager mechanism.  It creates a paged query with a fixed
 * number of entries per page and simultaneously counts the total number of rows.
 */
class PagerDefault_mysql extends PagerDefault {

  /**
   * Override the execute method.
   *
   * Before we run the query, we need to add pager-based range() instructions
   * to it.
   */
  public function execute() {
    global $pager_page_array, $pager_total, $pager_total_items, $pager_limits;

    // Add convenience tag to mark that this is an extended query. We have to
    // do this in the constructor to ensure that it is set before preExecute()
    // gets called.
    if (!$this->preExecute($this)) {
      return NULL;
    }

    // A NULL limit is the "kill switch" for pager queries.
    if (empty($this->limit)) {
      return;
    }
    $this->ensureElement();

    $page = isset($_GET['page']) ? $_GET['page'] : '';

    // Convert comma-separated $page to an array, used by other functions.
    $pager_page_array = explode(',', $page);

    if (!isset($pager_page_array[$this->element])) {
      $pager_page_array[$this->element] = 0;
    }

    // Rather than reply on the countQuery, add this flag to the query to prepare
    // for a FOUIND_ROWS() call a bit later on. That means we don't need to load
    // the entire table index twice. Win!
    //
    $this->addFlag('SQL_CALC_FOUND_ROWS');

    // We calculate the total of pages as ceil(items / limit).
    // Except before the query is run we don't know the total.
    //
    $pager_total_items[$this->element] = 0;
    $pager_total[$this->element] = ceil($pager_total_items[$this->element] / $this->limit);
    $pager_page_array[$this->element] = max(0, min((int)$pager_page_array[$this->element], ((int)$pager_total[$this->element]) - 1));
    $pager_limits[$this->element] = $this->limit;
    $this->range($pager_page_array[$this->element] * $this->limit, $this->limit);

    // Now that we've added our pager-based range instructions, run the query normally.
    $result = $this->query->execute();

    // At this point we need to redo the page calculation for the pager.
    //
    $pager_total_items[$this->element] = $this->connection->query('SELECT FOUND_ROWS()')->fetchField();
    $pager_total[$this->element] = ceil($pager_total_items[$this->element] / $this->limit);
    $pager_page_array[$this->element] = max(0, min((int)$pager_page_array[$this->element], ((int)$pager_total[$this->element]) - 1));

    // And return the query results.
    return $result;
  }
}
