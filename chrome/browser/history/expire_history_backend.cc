// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/expire_history_backend.h"

#include <algorithm>
#include <functional>
#include <limits>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/file_util.h"
#include "base/files/file_enumerator.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/history/archived_database.h"
#include "chrome/browser/history/history_database.h"
#include "chrome/browser/history/history_notifications.h"
#include "chrome/browser/history/thumbnail_database.h"
#include "components/bookmarks/core/browser/bookmark_service.h"

namespace history {

// Helpers --------------------------------------------------------------------

namespace {

// The number of days by which the expiration threshold is advanced for items
// that we want to expire early, such as those of AUTO_SUBFRAME transition type.
//
// Early expiration stuff is kept around only for edge cases, as subframes
// don't appear in history and the vast majority of them are ads anyway. The
// main use case for these is if you're on a site with links to different
// frames, you'll be able to see those links as visited, and we'll also be
// able to get redirect information for those URLs.
//
// But since these uses are most valuable when you're actually on the site,
// and because these can take up the bulk of your history, we get a lot of
// space savings by deleting them quickly.
const int kEarlyExpirationAdvanceDays = 3;

// Reads all types of visits starting from beginning of time to the given end
// time. This is the most general reader.
class AllVisitsReader : public ExpiringVisitsReader {
 public:
  virtual bool Read(base::Time end_time,
                    HistoryDatabase* db,
                    VisitVector* visits,
                    int max_visits) const OVERRIDE {
    DCHECK(db) << "must have a database to operate upon";
    DCHECK(visits) << "visit vector has to exist in order to populate it";

    db->GetAllVisitsInRange(base::Time(), end_time, max_visits, visits);
    // When we got the maximum number of visits we asked for, we say there could
    // be additional things to expire now.
    return static_cast<int>(visits->size()) == max_visits;
  }
};

// Reads only AUTO_SUBFRAME visits, within a computed range. The range is
// computed as follows:
// * |begin_time| is read from the meta table. This value is updated whenever
//   there are no more additional visits to expire by this reader.
// * |end_time| is advanced forward by a constant (kEarlyExpirationAdvanceDay),
//   but not past the current time.
class AutoSubframeVisitsReader : public ExpiringVisitsReader {
 public:
  virtual bool Read(base::Time end_time,
                    HistoryDatabase* db,
                    VisitVector* visits,
                    int max_visits) const OVERRIDE {
    DCHECK(db) << "must have a database to operate upon";
    DCHECK(visits) << "visit vector has to exist in order to populate it";

    base::Time begin_time = db->GetEarlyExpirationThreshold();
    // Advance |end_time| to expire early.
    base::Time early_end_time = end_time +
        base::TimeDelta::FromDays(kEarlyExpirationAdvanceDays);

    // We don't want to set the early expiration threshold to a time in the
    // future.
    base::Time now = base::Time::Now();
    if (early_end_time > now)
      early_end_time = now;

    db->GetVisitsInRangeForTransition(begin_time, early_end_time,
                                      max_visits,
                                      content::PAGE_TRANSITION_AUTO_SUBFRAME,
                                      visits);
    bool more = static_cast<int>(visits->size()) == max_visits;
    if (!more)
      db->UpdateEarlyExpirationThreshold(early_end_time);

    return more;
  }
};

// Returns true if this visit is worth archiving. Otherwise, this visit is not
// worth saving (for example, subframe navigations and redirects) and we can
// just delete it when it gets old.
bool ShouldArchiveVisit(const VisitRow& visit) {
  int no_qualifier = content::PageTransitionStripQualifier(visit.transition);

  // These types of transitions are always "important" and the user will want
  // to see them.
  if (no_qualifier == content::PAGE_TRANSITION_TYPED ||
      no_qualifier == content::PAGE_TRANSITION_AUTO_BOOKMARK ||
      no_qualifier == content::PAGE_TRANSITION_AUTO_TOPLEVEL)
    return true;

  // Only archive these "less important" transitions when they were the final
  // navigation and not part of a redirect chain.
  if ((no_qualifier == content::PAGE_TRANSITION_LINK ||
       no_qualifier == content::PAGE_TRANSITION_FORM_SUBMIT ||
       no_qualifier == content::PAGE_TRANSITION_KEYWORD ||
       no_qualifier == content::PAGE_TRANSITION_GENERATED) &&
      visit.transition & content::PAGE_TRANSITION_CHAIN_END)
    return true;

  // The transition types we ignore are AUTO_SUBFRAME and MANUAL_SUBFRAME.
  return false;
}

// The number of visits we will expire very time we check for old items. This
// Prevents us from doing too much work any given time.
const int kNumExpirePerIteration = 32;

// The number of seconds between checking for items that should be expired when
// we think there might be more items to expire. This timeout is used when the
// last expiration found at least kNumExpirePerIteration and we want to check
// again "soon."
const int kExpirationDelaySec = 30;

// The number of minutes between checking, as with kExpirationDelaySec, but
// when we didn't find enough things to expire last time. If there was no
// history to expire last iteration, it's likely there is nothing next
// iteration, so we want to wait longer before checking to avoid wasting CPU.
const int kExpirationEmptyDelayMin = 5;

}  // namespace


// ExpireHistoryBackend::DeleteEffects ----------------------------------------

ExpireHistoryBackend::DeleteEffects::DeleteEffects() {
}

ExpireHistoryBackend::DeleteEffects::~DeleteEffects() {
}


// ExpireHistoryBackend -------------------------------------------------------

ExpireHistoryBackend::ExpireHistoryBackend(
    BroadcastNotificationDelegate* delegate,
    BookmarkService* bookmark_service)
    : delegate_(delegate),
      main_db_(NULL),
      archived_db_(NULL),
      thumb_db_(NULL),
      weak_factory_(this),
      bookmark_service_(bookmark_service) {
}

ExpireHistoryBackend::~ExpireHistoryBackend() {
}

void ExpireHistoryBackend::SetDatabases(HistoryDatabase* main_db,
                                        ArchivedDatabase* archived_db,
                                        ThumbnailDatabase* thumb_db) {
  main_db_ = main_db;
  archived_db_ = archived_db;
  thumb_db_ = thumb_db;
}

void ExpireHistoryBackend::DeleteURL(const GURL& url) {
  DeleteURLs(std::vector<GURL>(1, url));
}

void ExpireHistoryBackend::DeleteURLs(const std::vector<GURL>& urls) {
  if (!main_db_)
    return;

  DeleteEffects effects;
  for (std::vector<GURL>::const_iterator url = urls.begin(); url != urls.end();
       ++url) {
    URLRow url_row;
    if (!main_db_->GetRowForURL(*url, &url_row))
      continue;  // Nothing to delete.

    // Collect all the visits and delete them. Note that we don't give
    // up if there are no visits, since the URL could still have an
    // entry that we should delete.  TODO(brettw): bug 1171148: We
    // should also delete from the archived DB.
    VisitVector visits;
    main_db_->GetVisitsForURL(url_row.id(), &visits);

    DeleteVisitRelatedInfo(visits, &effects);

    // We skip ExpireURLsForVisits (since we are deleting from the
    // URL, and not starting with visits in a given time range). We
    // therefore need to call the deletion and favicon update
    // functions manually.
    BookmarkService* bookmark_service = GetBookmarkService();
    DeleteOneURL(url_row,
                 bookmark_service && bookmark_service->IsBookmarked(*url),
                 &effects);
  }

  DeleteFaviconsIfPossible(&effects);

  BroadcastNotifications(&effects, DELETION_USER_INITIATED);
}

void ExpireHistoryBackend::ExpireHistoryBetween(
    const std::set<GURL>& restrict_urls,
    base::Time begin_time,
    base::Time end_time) {
  if (!main_db_)
    return;

  // Find the affected visits and delete them.
  // TODO(brettw): bug 1171164: We should query the archived database here, too.
  VisitVector visits;
  main_db_->GetAllVisitsInRange(begin_time, end_time, 0, &visits);
  if (!restrict_urls.empty()) {
    std::set<URLID> url_ids;
    for (std::set<GURL>::const_iterator url = restrict_urls.begin();
        url != restrict_urls.end(); ++url)
      url_ids.insert(main_db_->GetRowForURL(*url, NULL));
    VisitVector all_visits;
    all_visits.swap(visits);
    for (VisitVector::iterator visit = all_visits.begin();
         visit != all_visits.end(); ++visit) {
      if (url_ids.find(visit->url_id) != url_ids.end())
        visits.push_back(*visit);
    }
  }
  ExpireVisits(visits);
}

void ExpireHistoryBackend::ExpireHistoryForTimes(
    const std::vector<base::Time>& times) {
  // |times| must be in reverse chronological order and have no
  // duplicates, i.e. each member must be earlier than the one before
  // it.
  DCHECK(
      std::adjacent_find(
          times.begin(), times.end(), std::less_equal<base::Time>()) ==
      times.end());

  if (!main_db_)
    return;

  // Find the affected visits and delete them.
  // TODO(brettw): bug 1171164: We should query the archived database here, too.
  VisitVector visits;
  main_db_->GetVisitsForTimes(times, &visits);
  ExpireVisits(visits);
}

void ExpireHistoryBackend::ExpireVisits(const VisitVector& visits) {
  if (visits.empty())
    return;

  DeleteEffects effects;
  DeleteVisitRelatedInfo(visits, &effects);

  // Delete or update the URLs affected. We want to update the visit counts
  // since this is called by the user who wants to delete their recent history,
  // and we don't want to leave any evidence.
  ExpireURLsForVisits(visits, &effects);
  DeleteFaviconsIfPossible(&effects);
  BroadcastNotifications(&effects, DELETION_USER_INITIATED);

  // Pick up any bits possibly left over.
  ParanoidExpireHistory();
}

void ExpireHistoryBackend::ArchiveHistoryBefore(base::Time end_time) {
  if (!main_db_)
    return;

  // Archive as much history as possible before the given date.
  ArchiveSomeOldHistory(end_time, GetAllVisitsReader(),
                        std::numeric_limits<int>::max());
  ParanoidExpireHistory();
}

void ExpireHistoryBackend::InitWorkQueue() {
  DCHECK(work_queue_.empty()) << "queue has to be empty prior to init";

  for (size_t i = 0; i < readers_.size(); i++)
    work_queue_.push(readers_[i]);
}

const ExpiringVisitsReader* ExpireHistoryBackend::GetAllVisitsReader() {
  if (!all_visits_reader_)
    all_visits_reader_.reset(new AllVisitsReader());
  return all_visits_reader_.get();
}

const ExpiringVisitsReader*
    ExpireHistoryBackend::GetAutoSubframeVisitsReader() {
  if (!auto_subframe_visits_reader_)
    auto_subframe_visits_reader_.reset(new AutoSubframeVisitsReader());
  return auto_subframe_visits_reader_.get();
}

void ExpireHistoryBackend::StartArchivingOldStuff(
    base::TimeDelta expiration_threshold) {
  expiration_threshold_ = expiration_threshold;

  // Remove all readers, just in case this was method was called before.
  readers_.clear();
  // For now, we explicitly add all known readers. If we come up with more
  // reader types (in case we want to expire different types of visits in
  // different ways), we can make it be populated by creator/owner of
  // ExpireHistoryBackend.
  readers_.push_back(GetAllVisitsReader());
  readers_.push_back(GetAutoSubframeVisitsReader());

  // Initialize the queue with all tasks for the first set of iterations.
  InitWorkQueue();
  ScheduleArchive();
}

void ExpireHistoryBackend::DeleteFaviconsIfPossible(DeleteEffects* effects) {
  if (!thumb_db_)
    return;

  for (std::set<favicon_base::FaviconID>::const_iterator i =
           effects->affected_favicons.begin();
       i != effects->affected_favicons.end(); ++i) {
    if (!thumb_db_->HasMappingFor(*i)) {
      GURL icon_url;
      favicon_base::IconType icon_type;
      if (thumb_db_->GetFaviconHeader(*i,
                                      &icon_url,
                                      &icon_type) &&
          thumb_db_->DeleteFavicon(*i)) {
        effects->deleted_favicons.insert(icon_url);
      }
    }
  }
}

void ExpireHistoryBackend::BroadcastNotifications(DeleteEffects* effects,
                                                  DeletionType type) {
  if (!effects->modified_urls.empty()) {
    scoped_ptr<URLsModifiedDetails> details(new URLsModifiedDetails);
    details->changed_urls = effects->modified_urls;
    delegate_->NotifySyncURLsModified(&details->changed_urls);
    delegate_->BroadcastNotifications(
        chrome::NOTIFICATION_HISTORY_URLS_MODIFIED,
        details.PassAs<HistoryDetails>());
  }
  if (!effects->deleted_urls.empty()) {
    scoped_ptr<URLsDeletedDetails> details(new URLsDeletedDetails);
    details->all_history = false;
    details->archived = (type == DELETION_ARCHIVED);
    details->rows = effects->deleted_urls;
    details->favicon_urls = effects->deleted_favicons;
    delegate_->NotifySyncURLsDeleted(details->all_history, details->archived,
                                     &details->rows);
    delegate_->BroadcastNotifications(chrome::NOTIFICATION_HISTORY_URLS_DELETED,
                                      details.PassAs<HistoryDetails>());
  }
}

void ExpireHistoryBackend::DeleteVisitRelatedInfo(const VisitVector& visits,
                                                  DeleteEffects* effects) {
  for (size_t i = 0; i < visits.size(); i++) {
    // Delete the visit itself.
    main_db_->DeleteVisit(visits[i]);

    // Add the URL row to the affected URL list.
    if (!effects->affected_urls.count(visits[i].url_id)) {
      URLRow row;
      if (main_db_->GetURLRow(visits[i].url_id, &row))
        effects->affected_urls[visits[i].url_id] = row;
    }
  }
}

void ExpireHistoryBackend::DeleteOneURL(const URLRow& url_row,
                                        bool is_bookmarked,
                                        DeleteEffects* effects) {
  main_db_->DeleteSegmentForURL(url_row.id());

  if (!is_bookmarked) {
    effects->deleted_urls.push_back(url_row);

    // Delete stuff that references this URL.
    if (thumb_db_) {
      // Collect shared information.
      std::vector<IconMapping> icon_mappings;
      if (thumb_db_->GetIconMappingsForPageURL(url_row.url(), &icon_mappings)) {
        for (std::vector<IconMapping>::iterator m = icon_mappings.begin();
             m != icon_mappings.end(); ++m) {
          effects->affected_favicons.insert(m->icon_id);
        }
        // Delete the mapping entries for the url.
        thumb_db_->DeleteIconMappings(url_row.url());
      }
    }
    // Last, delete the URL entry.
    main_db_->DeleteURLRow(url_row.id());
  }
}

URLID ExpireHistoryBackend::ArchiveOneURL(const URLRow& url_row) {
  if (!archived_db_)
    return 0;

  // See if this URL is present in the archived database already. Note that
  // we must look up by ID since the URL ID will be different.
  URLRow archived_row;
  if (archived_db_->GetRowForURL(url_row.url(), &archived_row)) {
    // TODO(sky): bug 1168470, need to archive past search terms.
    // TODO(brettw): should be copy the visit counts over? This will mean that
    // the main DB's visit counts are only for the last 3 months rather than
    // accumulative.
    archived_row.set_last_visit(url_row.last_visit());
    archived_db_->UpdateURLRow(archived_row.id(), archived_row);
    return archived_row.id();
  }

  // This row is not in the archived DB, add it.
  return archived_db_->AddURL(url_row);
}

namespace {

struct ChangedURL {
  ChangedURL() : visit_count(0), typed_count(0) {}
  int visit_count;
  int typed_count;
};

}  // namespace

void ExpireHistoryBackend::ExpireURLsForVisits(const VisitVector& visits,
                                               DeleteEffects* effects) {
  // First find all unique URLs and the number of visits we're deleting for
  // each one.
  std::map<URLID, ChangedURL> changed_urls;
  for (size_t i = 0; i < visits.size(); i++) {
    ChangedURL& cur = changed_urls[visits[i].url_id];
    // NOTE: This code must stay in sync with HistoryBackend::AddPageVisit().
    // TODO(pkasting): http://b/1148304 We shouldn't be marking so many URLs as
    // typed, which would help eliminate the need for this code (we still would
    // need to handle RELOAD transitions specially, though).
    content::PageTransition transition =
        content::PageTransitionStripQualifier(visits[i].transition);
    if (transition != content::PAGE_TRANSITION_RELOAD)
      cur.visit_count++;
    if ((transition == content::PAGE_TRANSITION_TYPED &&
        !content::PageTransitionIsRedirect(visits[i].transition)) ||
        transition == content::PAGE_TRANSITION_KEYWORD_GENERATED)
      cur.typed_count++;
  }

  // Check each unique URL with deleted visits.
  BookmarkService* bookmark_service = GetBookmarkService();
  for (std::map<URLID, ChangedURL>::const_iterator i = changed_urls.begin();
       i != changed_urls.end(); ++i) {
    // The unique URL rows should already be filled in.
    URLRow& url_row = effects->affected_urls[i->first];
    if (!url_row.id())
      continue;  // URL row doesn't exist in the database.

    // Check if there are any other visits for this URL and update the time
    // (the time change may not actually be synced to disk below when we're
    // archiving).
    VisitRow last_visit;
    if (main_db_->GetMostRecentVisitForURL(url_row.id(), &last_visit))
      url_row.set_last_visit(last_visit.visit_time);
    else
      url_row.set_last_visit(base::Time());

    // Don't delete URLs with visits still in the DB, or bookmarked.
    bool is_bookmarked =
        (bookmark_service && bookmark_service->IsBookmarked(url_row.url()));
    if (!is_bookmarked && url_row.last_visit().is_null()) {
      // Not bookmarked and no more visits. Nuke the url.
      DeleteOneURL(url_row, is_bookmarked, effects);
    } else {
      // NOTE: The calls to std::max() below are a backstop, but they should
      // never actually be needed unless the database is corrupt (I think).
      url_row.set_visit_count(
          std::max(0, url_row.visit_count() - i->second.visit_count));
      url_row.set_typed_count(
          std::max(0, url_row.typed_count() - i->second.typed_count));

      // Update the db with the new details.
      main_db_->UpdateURLRow(url_row.id(), url_row);

      effects->modified_urls.push_back(url_row);
    }
  }
}

void ExpireHistoryBackend::ArchiveURLsAndVisits(const VisitVector& visits) {
  if (!archived_db_ || !main_db_)
    return;

  // Make sure all unique URL rows are added to the dependency list and the
  // archived database. We will also keep the mapping between the main DB URLID
  // and the archived one.
  std::map<URLID, URLID> main_id_to_archived_id;
  for (size_t i = 0; i < visits.size(); i++) {
    if (!main_id_to_archived_id.count(visits[i].url_id)) {
      // Unique URL encountered, archive it.
      // Only add URL to the dependency list once we know we successfully
      // archived it.
      URLRow row;
      if (main_db_->GetURLRow(visits[i].url_id, &row)) {
        URLID archived_id = ArchiveOneURL(row);
        if (archived_id)
          main_id_to_archived_id[row.id()] = archived_id;
      }
    }
  }

  // Retrieve the sources for all the archived visits before archiving.
  // The returned visit_sources vector should contain the source for each visit
  // from visits at the same index.
  VisitSourceMap visit_sources;
  main_db_->GetVisitsSource(visits, &visit_sources);

  // Now archive the visits since we know the URL ID to make them reference.
  // The source visit list should still reference the visits in the main DB, but
  // we will update it to reflect only the visits that were successfully
  // archived.
  for (size_t i = 0; i < visits.size(); i++) {
    // Construct the visit that we will add to the archived database. We do
    // not store referring visits since we delete many of the visits when
    // archiving.
    VisitRow cur_visit(visits[i]);
    cur_visit.url_id = main_id_to_archived_id[cur_visit.url_id];
    cur_visit.referring_visit = 0;
    VisitSourceMap::iterator iter = visit_sources.find(visits[i].visit_id);
    archived_db_->AddVisit(
        &cur_visit,
        iter == visit_sources.end() ? SOURCE_BROWSED : iter->second);
    // Ignore failures, we will delete it from the main DB no matter what.
  }
}

void ExpireHistoryBackend::ScheduleArchive() {
  base::TimeDelta delay;
  if (work_queue_.empty()) {
    // If work queue is empty, reset the work queue to contain all tasks and
    // schedule next iteration after a longer delay.
    InitWorkQueue();
    delay = base::TimeDelta::FromMinutes(kExpirationEmptyDelayMin);
  } else {
    delay = base::TimeDelta::FromSeconds(kExpirationDelaySec);
  }

  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&ExpireHistoryBackend::DoArchiveIteration,
                 weak_factory_.GetWeakPtr()),
      delay);
}

void ExpireHistoryBackend::DoArchiveIteration() {
  DCHECK(!work_queue_.empty()) << "queue has to be non-empty";

  const ExpiringVisitsReader* reader = work_queue_.front();
  bool more_to_expire = ArchiveSomeOldHistory(GetCurrentArchiveTime(), reader,
                                              kNumExpirePerIteration);

  work_queue_.pop();
  // If there are more items to expire, add the reader back to the queue, thus
  // creating a new task for future iterations.
  if (more_to_expire)
    work_queue_.push(reader);

  ScheduleArchive();
}

bool ExpireHistoryBackend::ArchiveSomeOldHistory(
    base::Time end_time,
    const ExpiringVisitsReader* reader,
    int max_visits) {
  if (!main_db_)
    return false;

  // Add an extra time unit to given end time, because
  // GetAllVisitsInRange, et al. queries' end value is non-inclusive.
  base::Time effective_end_time =
      base::Time::FromInternalValue(end_time.ToInternalValue() + 1);

  VisitVector affected_visits;
  bool more_to_expire = reader->Read(effective_end_time, main_db_,
                                     &affected_visits, max_visits);

  // Some visits we'll delete while others we'll archive.
  VisitVector deleted_visits, archived_visits;
  for (size_t i = 0; i < affected_visits.size(); i++) {
    if (ShouldArchiveVisit(affected_visits[i]))
      archived_visits.push_back(affected_visits[i]);
    else
      deleted_visits.push_back(affected_visits[i]);
  }

  // Do the actual archiving.
  ArchiveURLsAndVisits(archived_visits);

  // Delete all the visits.
  deleted_visits.insert(deleted_visits.end(), archived_visits.begin(),
                        archived_visits.end());
  DeleteEffects deleted_effects;
  DeleteVisitRelatedInfo(deleted_visits, &deleted_effects);
  ExpireURLsForVisits(deleted_visits, &deleted_effects);
  DeleteFaviconsIfPossible(&deleted_effects);
  BroadcastNotifications(&deleted_effects, DELETION_ARCHIVED);

  return more_to_expire;
}

void ExpireHistoryBackend::ParanoidExpireHistory() {
  // TODO(brettw): Bug 1067331: write this to clean up any errors.
}

BookmarkService* ExpireHistoryBackend::GetBookmarkService() {
  // We use the bookmark service to determine if a URL is bookmarked. The
  // bookmark service is loaded on a separate thread and may not be done by the
  // time we get here. We therefor block until the bookmarks have finished
  // loading.
  if (bookmark_service_)
    bookmark_service_->BlockTillLoaded();
  return bookmark_service_;
}

}  // namespace history
